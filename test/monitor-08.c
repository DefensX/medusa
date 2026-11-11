#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

#if defined(__WINDOWS__)
#include <windows.h>
#else
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#endif

#include "medusa/error.h"
#include "medusa/io.h"
#include "medusa/condition.h"
#include "medusa/monitor.h"

#include "medusa/queue.h"
#include "medusa/monitor-private.h"
#include "medusa/subject-struct.h"

/*
 * how the monitor wakes up.
 *
 * a change made on the loop thread writes no wakeup, and the poll after it
 * does not block. a change from another thread wakes a loop that is blocked
 * in poll, through a pipe where there is one, or within the wakeup interval
 * where there is none (windows, or MEDUSA_MONITOR_WAKEUP_NONE).
 */

/* long enough to tell a bounded poll from a poll that did not block, also with a 15.6ms tick */
#define INTERVAL        0.100

/* what a busy machine may add to any expected time */
#define SLACK           0.300

/* how long the helper thread waits before it acts, so the loop is blocked in poll by then */
#define DELAY           0.200

static const unsigned int g_polls[] = {
        MEDUSA_MONITOR_POLL_DEFAULT,
#if defined(__LINUX__)
        MEDUSA_MONITOR_POLL_EPOLL,
#endif
#if defined(__APPLE__)
        MEDUSA_MONITOR_POLL_KQUEUE,
#endif
#if defined(__LINUX__) || defined(__APPLE__)
        MEDUSA_MONITOR_POLL_POLL,
#endif
        MEDUSA_MONITOR_POLL_SELECT,
#if defined(__WINDOWS__)
        MEDUSA_MONITOR_POLL_WSAPOLL,
#endif
};

static const unsigned int g_wakeups[] = {
        MEDUSA_MONITOR_WAKEUP_DEFAULT,
        MEDUSA_MONITOR_WAKEUP_PIPE,
        MEDUSA_MONITOR_WAKEUP_NONE,
};

static double now (void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
}

#if !defined(__WINDOWS__)

static double cpu (void)
{
        struct timespec ts;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* the read end of the wakeup pipe, the only io of the monitor that is a fifo */
static int find_wakeup_fd (struct medusa_monitor *monitor)
{
        int fd;
        struct stat st;
        struct medusa_subject *subject;
        for (subject = medusa_monitor_get_first_subject_unlocked(monitor); subject; subject = medusa_monitor_get_next_subject_unlocked(subject)) {
                if (medusa_subject_get_type(subject) != MEDUSA_SUBJECT_TYPE_IO) {
                        continue;
                }
                fd = medusa_io_get_fd((struct medusa_io *) subject);
                if (fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode)) {
                        return fd;
                }
        }
        return -1;
}

/* bytes waiting in the wakeup pipe, -1 if there is no pipe */
static int queued (struct medusa_monitor *monitor)
{
        int fd;
        int count;
        fd = find_wakeup_fd(monitor);
        if (fd < 0) {
                return -1;
        }
        if (ioctl(fd, FIONREAD, &count) != 0) {
                return -1;
        }
        return count;
}

#endif

#if defined(__LINUX__)

static int count_fds (void)
{
        int count;
        DIR *dir;
        count = 0;
        dir = opendir("/proc/self/fd");
        if (dir == NULL) {
                return -1;
        }
        while (readdir(dir) != NULL) {
                count++;
        }
        closedir(dir);
        return count;
}

#endif

enum {
        ACTION_BREAK,
        ACTION_CONTINUE,
        ACTION_CONTINUE_MANY,
        ACTION_CONDITION
};

struct helper {
        struct medusa_monitor *monitor;
        struct medusa_condition *condition;
        int action;
        double delay;
        int rc;
};

static void * helper_routine (void *arg)
{
        int i;
        struct helper *helper = arg;
        if (helper->delay > 0) {
                usleep(helper->delay * 1e6);
        }
        switch (helper->action) {
                case ACTION_BREAK:
                        helper->rc = medusa_monitor_break(helper->monitor);
                        break;
                case ACTION_CONTINUE:
                        helper->rc = medusa_monitor_continue(helper->monitor);
                        break;
                case ACTION_CONTINUE_MANY:
                        for (i = 0; i < 1000; i++) {
                                helper->rc = medusa_monitor_continue(helper->monitor);
                                if (helper->rc != 0) {
                                        break;
                                }
                        }
                        break;
                case ACTION_CONDITION:
                        helper->rc = medusa_condition_signal(helper->condition);
                        break;
        }
        return NULL;
}

static int helper_start (pthread_t *thread, struct helper *helper, struct medusa_monitor *monitor, int action, double delay)
{
        helper->monitor = monitor;
        helper->action  = action;
        helper->delay   = delay;
        helper->rc      = -1;
        return pthread_create(thread, NULL, helper_routine, helper);
}

/* the wakeup a monitor resolves to */
static unsigned int expected_wakeup (unsigned int wakeup)
{
        if (wakeup == MEDUSA_MONITOR_WAKEUP_DEFAULT) {
#if defined(__WINDOWS__)
                return MEDUSA_MONITOR_WAKEUP_NONE;
#else
                return MEDUSA_MONITOR_WAKEUP_PIPE;
#endif
        }
        return wakeup;
}

/* let pending changes be applied, so the next poll would block */
static int settle (struct medusa_monitor *monitor)
{
        int i;
        int rc;
        for (i = 0; i < 4; i++) {
                rc = medusa_monitor_run_timeout(monitor, 0);
                if (rc != 1) {
                        fprintf(stderr, "    medusa_monitor_run_timeout failed, rc: %d\n", rc);
                        return -1;
                }
        }
        return 0;
}

/* a change of the loop thread writes no wakeup, and the loop does not sleep before it is applied */
static int check_own (struct medusa_monitor *monitor, unsigned int wakeup)
{
        int rc;
        double start;
        double elapsed;

        (void) wakeup;

        if (settle(monitor) != 0) {
                return -1;
        }

        rc = medusa_monitor_continue(monitor);
        if (rc != 0) {
                fprintf(stderr, "    medusa_monitor_continue failed, rc: %d\n", rc);
                return -1;
        }
#if !defined(__WINDOWS__)
        if (wakeup == MEDUSA_MONITOR_WAKEUP_PIPE && queued(monitor) != 0) {
                fprintf(stderr, "    a wakeup is written for a continue of the loop thread, queued: %d\n", queued(monitor));
                return -1;
        }
#endif
        start = now();
        rc = medusa_monitor_run_timeout(monitor, 10.0);
        elapsed = now() - start;
        if (rc != 1) {
                fprintf(stderr, "    medusa_monitor_run_timeout is not running, rc: %d\n", rc);
                return -1;
        }
        if (elapsed > INTERVAL / 2) {
                fprintf(stderr, "    medusa_monitor_run_timeout blocked with a pending change, elapsed: %.3f\n", elapsed);
                return -1;
        }

        rc = medusa_monitor_break(monitor);
        if (rc != 0) {
                fprintf(stderr, "    medusa_monitor_break failed, rc: %d\n", rc);
                return -1;
        }
#if !defined(__WINDOWS__)
        if (wakeup == MEDUSA_MONITOR_WAKEUP_PIPE && queued(monitor) != 0) {
                fprintf(stderr, "    a wakeup is written for a break of the loop thread, queued: %d\n", queued(monitor));
                return -1;
        }
#endif
        start = now();
        rc = medusa_monitor_run_timeout(monitor, 10.0);
        elapsed = now() - start;
        if (rc != 0) {
                fprintf(stderr, "    medusa_monitor_run_timeout did not break, rc: %d\n", rc);
                return -1;
        }
        if (elapsed > INTERVAL / 2) {
                fprintf(stderr, "    medusa_monitor_run_timeout blocked with a break, elapsed: %.3f\n", elapsed);
                return -1;
        }
        return medusa_monitor_continue(monitor);
}

/* a break from another thread ends a loop that is blocked in poll */
static int check_remote_break (struct medusa_monitor *monitor, unsigned int wakeup)
{
        int rc;
        int runs;
        double start;
        double late;
        pthread_t thread;
        struct helper helper;

        if (settle(monitor) != 0) {
                return -1;
        }
        if (helper_start(&thread, &helper, monitor, ACTION_BREAK, DELAY) != 0) {
                fprintf(stderr, "    can not create thread\n");
                return -1;
        }
        runs  = 0;
        start = now();
        do {
                rc = medusa_monitor_run_timeout(monitor, 10.0);
                runs++;
        } while (rc == 1 && now() - start < DELAY + 2.0);
        late = now() - start - DELAY;
        pthread_join(thread, NULL);
        if (helper.rc != 0) {
                fprintf(stderr, "    medusa_monitor_break failed, rc: %d\n", helper.rc);
                return -1;
        }
        if (rc != 0) {
                fprintf(stderr, "    medusa_monitor_run_timeout did not break, rc: %d\n", rc);
                return -1;
        }
        if (wakeup == MEDUSA_MONITOR_WAKEUP_PIPE) {
                if (runs != 1) {
                        fprintf(stderr, "    medusa_monitor_run_timeout returned without a reason, runs: %d\n", runs);
                        return -1;
                }
                if (late > SLACK) {
                        fprintf(stderr, "    break is noticed late, late: %.3f\n", late);
                        return -1;
                }
        } else {
                if (late > INTERVAL + SLACK) {
                        fprintf(stderr, "    break is noticed late, late: %.3f\n", late);
                        return -1;
                }
        }
        return medusa_monitor_continue(monitor);
}

static int condition_onevent (struct medusa_condition *condition, unsigned int events, void *context, void *param)
{
        int *signalled = context;
        (void) condition;
        (void) param;
        if (events & MEDUSA_CONDITION_EVENT_SIGNAL) {
                *signalled += 1;
        }
        return 0;
}

/* a condition signalled from another thread is delivered to a loop that is blocked in poll */
static int check_remote_condition (struct medusa_monitor *monitor, unsigned int wakeup)
{
        int rc;
        int signalled;
        double start;
        double late;
        pthread_t thread;
        struct helper helper;
        struct medusa_condition *condition;

        signalled = 0;
        condition = medusa_condition_create(monitor, condition_onevent, &signalled);
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                fprintf(stderr, "    medusa_condition_create failed\n");
                return -1;
        }
        rc = medusa_condition_set_enabled(condition, 1);
        if (rc != 0) {
                fprintf(stderr, "    medusa_condition_set_enabled failed, rc: %d\n", rc);
                goto bail;
        }
        if (settle(monitor) != 0) {
                goto bail;
        }
        helper.condition = condition;
        if (helper_start(&thread, &helper, monitor, ACTION_CONDITION, DELAY) != 0) {
                fprintf(stderr, "    can not create thread\n");
                goto bail;
        }
        start = now();
        do {
                rc = medusa_monitor_run_timeout(monitor, 10.0);
        } while (rc == 1 && signalled == 0 && now() - start < DELAY + 2.0);
        late = now() - start - DELAY;
        pthread_join(thread, NULL);
        if (helper.rc != 0) {
                fprintf(stderr, "    medusa_condition_signal failed, rc: %d\n", helper.rc);
                goto bail;
        }
        if (rc != 1) {
                fprintf(stderr, "    medusa_monitor_run_timeout failed, rc: %d\n", rc);
                goto bail;
        }
        if (signalled != 1) {
                fprintf(stderr, "    condition is not delivered, signalled: %d\n", signalled);
                goto bail;
        }
        if (late > ((wakeup == MEDUSA_MONITOR_WAKEUP_PIPE) ? 0 : INTERVAL) + SLACK) {
                fprintf(stderr, "    condition is delivered late, late: %.3f\n", late);
                goto bail;
        }
        medusa_condition_destroy(condition);
        return 0;
bail:   medusa_condition_destroy(condition);
        return -1;
}

/* an idle loop blocks with a pipe, and wakes up once per interval with none, it never spins */
static int check_idle (struct medusa_monitor *monitor, unsigned int wakeup)
{
        int rc;
        int runs;
        double start;
        double elapsed;
#if !defined(__WINDOWS__)
        double cpu0;
#endif

        if (settle(monitor) != 0) {
                return -1;
        }
        if (wakeup == MEDUSA_MONITOR_WAKEUP_PIPE) {
                start = now();
                rc = medusa_monitor_run_timeout(monitor, 0.5);
                elapsed = now() - start;
                if (rc != 1) {
                        fprintf(stderr, "    medusa_monitor_run_timeout failed, rc: %d\n", rc);
                        return -1;
                }
                if (elapsed < 0.4) {
                        fprintf(stderr, "    idle loop did not block, elapsed: %.3f\n", elapsed);
                        return -1;
                }
                return 0;
        }
        runs  = 0;
#if !defined(__WINDOWS__)
        cpu0  = cpu();
#endif
        start = now();
        while (now() - start < 1.0) {
                rc = medusa_monitor_run_timeout(monitor, 10.0);
                if (rc != 1) {
                        fprintf(stderr, "    medusa_monitor_run_timeout failed, rc: %d\n", rc);
                        return -1;
                }
                runs++;
        }
        /* about 1.0 / INTERVAL runs, a 15.6ms tick may stretch each */
        if (runs < (int) (0.5 / INTERVAL) || runs > (int) (2.0 / INTERVAL)) {
                fprintf(stderr, "    idle loop is not paced by the interval, runs: %d\n", runs);
                return -1;
        }
#if !defined(__WINDOWS__)
        if (cpu() - cpu0 > 0.2) {
                fprintf(stderr, "    idle loop spins, cpu: %.3f\n", cpu() - cpu0);
                return -1;
        }
#endif
        return 0;
}

#if !defined(__WINDOWS__)

/* wakeups from another thread while the loop is not polling are merged into one byte */
static int check_merged (struct medusa_monitor *monitor)
{
        int rc;
        double start;
        double elapsed;
        pthread_t thread;
        struct helper helper;

        if (settle(monitor) != 0) {
                return -1;
        }
        if (helper_start(&thread, &helper, monitor, ACTION_CONTINUE_MANY, 0) != 0) {
                fprintf(stderr, "    can not create thread\n");
                return -1;
        }
        pthread_join(thread, NULL);
        if (helper.rc != 0) {
                fprintf(stderr, "    medusa_monitor_continue failed, rc: %d\n", helper.rc);
                return -1;
        }
        if (queued(monitor) != (int) sizeof(unsigned int)) {
                fprintf(stderr, "    wakeups are not merged, queued: %d\n", queued(monitor));
                return -1;
        }
        rc = medusa_monitor_run_timeout(monitor, 10.0);
        if (rc != 1) {
                fprintf(stderr, "    medusa_monitor_run_timeout failed, rc: %d\n", rc);
                return -1;
        }
        if (queued(monitor) != 0) {
                fprintf(stderr, "    wakeup is not read, queued: %d\n", queued(monitor));
                return -1;
        }
        /* the wakeup is consumed, the next poll blocks */
        start = now();
        rc = medusa_monitor_run_timeout(monitor, 0.5);
        elapsed = now() - start;
        if (rc != 1) {
                fprintf(stderr, "    medusa_monitor_run_timeout failed, rc: %d\n", rc);
                return -1;
        }
        if (elapsed < 0.4) {
                fprintf(stderr, "    loop did not block after the wakeup is read, elapsed: %.3f\n", elapsed);
                return -1;
        }
        /* and a new wakeup is written once the loop has read the last one */
        if (helper_start(&thread, &helper, monitor, ACTION_CONTINUE, 0) != 0) {
                fprintf(stderr, "    can not create thread\n");
                return -1;
        }
        pthread_join(thread, NULL);
        if (queued(monitor) != (int) sizeof(unsigned int)) {
                fprintf(stderr, "    wakeup is not written again, queued: %d\n", queued(monitor));
                return -1;
        }
        return 0;
}

#endif

static int test_wakeup (unsigned int poll, unsigned int wakeup)
{
        int rc;
        unsigned int type;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        fprintf(stderr, "  wakeup: %s\n", medusa_monitor_wakeup_type_string(wakeup));

        medusa_monitor_init_options_default(&options);
        options.poll.type       = poll;
        /* the signal backend has a pipe of its own, it would be taken for the wakeup */
        options.signal.type     = MEDUSA_MONITOR_SIGNAL_NULL;
        options.wakeup.type     = wakeup;
        options.wakeup.interval = INTERVAL;

        monitor = medusa_monitor_create_with_options(&options);
#if defined(__WINDOWS__)
        if (wakeup == MEDUSA_MONITOR_WAKEUP_PIPE) {
                if (!MEDUSA_IS_ERR_OR_NULL(monitor)) {
                        fprintf(stderr, "    a pipe wakeup is created on windows\n");
                        medusa_monitor_destroy(monitor);
                        return -1;
                }
                fprintf(stderr, "    not supported\n");
                return 0;
        }
#endif
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "    medusa_monitor_create failed\n");
                return -1;
        }

        type = medusa_monitor_get_wakeup_type(monitor);
        if (type != expected_wakeup(wakeup)) {
                fprintf(stderr, "    wakeup is: %s, expected: %s\n", medusa_monitor_wakeup_type_string(type), medusa_monitor_wakeup_type_string(expected_wakeup(wakeup)));
                goto bail;
        }
#if !defined(__WINDOWS__)
        if ((type == MEDUSA_MONITOR_WAKEUP_PIPE) != (find_wakeup_fd(monitor) >= 0)) {
                fprintf(stderr, "    wakeup pipe does not match the wakeup type\n");
                goto bail;
        }
#endif

        rc = check_own(monitor, type);
        if (rc != 0) {
                fprintf(stderr, "    check own failed\n");
                goto bail;
        }
        rc = check_remote_break(monitor, type);
        if (rc != 0) {
                fprintf(stderr, "    check remote break failed\n");
                goto bail;
        }
        rc = check_remote_condition(monitor, type);
        if (rc != 0) {
                fprintf(stderr, "    check remote condition failed\n");
                goto bail;
        }
        rc = check_idle(monitor, type);
        if (rc != 0) {
                fprintf(stderr, "    check idle failed\n");
                goto bail;
        }
#if !defined(__WINDOWS__)
        if (type == MEDUSA_MONITOR_WAKEUP_PIPE) {
                rc = check_merged(monitor);
                if (rc != 0) {
                        fprintf(stderr, "    check merged failed\n");
                        goto bail;
                }
        }
#endif

        medusa_monitor_destroy(monitor);
        return 0;
bail:   medusa_monitor_destroy(monitor);
        return -1;
}

static int test_invalid (void)
{
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;
        medusa_monitor_init_options_default(&options);
        options.wakeup.type = 0x1000;
        monitor = medusa_monitor_create_with_options(&options);
        if (!MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "a monitor is created with an invalid wakeup type\n");
                medusa_monitor_destroy(monitor);
                return -1;
        }
        if (medusa_monitor_wakeup_type_value("pipe") != MEDUSA_MONITOR_WAKEUP_PIPE ||
            medusa_monitor_wakeup_type_value("MEDUSA_MONITOR_WAKEUP_NONE") != MEDUSA_MONITOR_WAKEUP_NONE ||
            medusa_monitor_wakeup_type_value("bogus") != -EINVAL) {
                fprintf(stderr, "medusa_monitor_wakeup_type_value is wrong\n");
                return -1;
        }
        return 0;
}

#if !defined(__WINDOWS__)

static void alarm_handler (int sig)
{
        (void) sig;
        abort();
}

#endif

int main (int argc, char *argv[])
{
        int rc;
        int failed;
        unsigned int p;
        unsigned int w;
#if defined(__LINUX__)
        int before;
        int after;
#endif

        (void) argc;
        (void) argv;

#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#else
        signal(SIGALRM, alarm_handler);
#endif

        failed = 0;
#if defined(__LINUX__)
        before = count_fds();
#endif
        if (test_invalid() != 0) {
                failed++;
        }
        for (p = 0; p < sizeof(g_polls) / sizeof(g_polls[0]); p++) {
                fprintf(stderr, "testing poll: %s\n", medusa_monitor_poll_type_string(g_polls[p]));
                for (w = 0; w < sizeof(g_wakeups) / sizeof(g_wakeups[0]); w++) {
#if !defined(__WINDOWS__)
                        alarm(30);
#endif
                        rc = test_wakeup(g_polls[p], g_wakeups[w]);
                        if (rc != 0) {
                                fprintf(stderr, "  failed\n");
                                failed++;
                        }
                }
        }
#if defined(__LINUX__)
        after = count_fds();
        if (before != after) {
                fprintf(stderr, "fds are leaked, before: %d, after: %d\n", before, after);
                failed++;
        }
#endif
        if (failed) {
                fprintf(stderr, "failed: %d\n", failed);
                return -1;
        }
        fprintf(stderr, "success\n");
        return 0;
}
