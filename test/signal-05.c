
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include <sys/types.h>
#include <pthread.h>

#include "medusa/error.h"
#include "medusa/timer.h"
#include "medusa/signal.h"
#include "medusa/monitor.h"

#define NMONITORS       32

static const unsigned int g_polls[] = {
        MEDUSA_MONITOR_POLL_DEFAULT,
#if defined(__LINUX__)
        MEDUSA_MONITOR_POLL_EPOLL,
#endif
#if defined(__APPLE__)
        MEDUSA_MONITOR_POLL_KQUEUE,
#endif
        MEDUSA_MONITOR_POLL_POLL,
        MEDUSA_MONITOR_POLL_SELECT
};

static int signal_onevent (struct medusa_signal *signal, unsigned int events, void *context, void *param)
{
        (void) signal;
        (void) events;
        (void) context;
        (void) param;
        if (events & MEDUSA_SIGNAL_EVENT_FIRED) {
                fprintf(stderr, "signal on event\n");
                return medusa_monitor_break(medusa_signal_get_monitor(signal));
        }
        return 0;
}

static int timer_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        pid_t pid;
        (void) timer;
        (void) events;
        (void) context;
        (void) param;
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                fprintf(stderr, "send signal\n");
                pid = getpid();
                kill(pid, SIGUSR1);
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int i;
        int rc;

        struct medusa_monitor *monitor[NMONITORS];
        struct medusa_monitor_init_options options[NMONITORS];

        struct medusa_timer *timer[NMONITORS];
        struct medusa_signal *signal[NMONITORS];

        for (i = 0; i < NMONITORS; i++) {
                monitor[i] = NULL;
        }

        fprintf(stderr, "creating monitors: %d\n", NMONITORS);
        for (i = 0; i < NMONITORS; i++) {
                medusa_monitor_init_options_default(&options[i]);
                options[i].poll.type = poll;

                monitor[i] = medusa_monitor_create_with_options(&options[i]);
                if (monitor[i] == NULL) {
                        goto bail;
                }
        }

        fprintf(stderr, "creating signals\n");
        for (i = 0; i < NMONITORS; i++) {
                signal[i] = medusa_signal_create(monitor[i], SIGUSR1, signal_onevent, NULL);
                if (MEDUSA_IS_ERR_OR_NULL(signal[i])) {
                        if (MEDUSA_PTR_ERR(signal[i]) == -ENOENT) {
                                fprintf(stderr, "monitor: %d does not support signal\n", i);
                                continue;
                        }
                        goto bail;
                }
                rc = medusa_signal_set_enabled(signal[i], 1);
                if (rc < 0) {
                        goto bail;
                }

                timer[i] = medusa_timer_create_singleshot(monitor[i], 0.1, timer_onevent, NULL);
                if (MEDUSA_IS_ERR_OR_NULL(timer[i])) {
                        goto bail;
                }
        }

        fprintf(stderr, "running monitors\n");
        while (1) {
                for (i = 0; i < NMONITORS; i++) {
                        rc = medusa_monitor_run_timeout(monitor[i], 0.001);
                        if (rc == 0) {
                                fprintf(stderr, "monitor: %p run break\n", monitor[i]);
                                break;
                        }
                        if (rc < 0) {
                                fprintf(stderr, "monitor: %p run failed\n", monitor[i]);
                                goto bail;
                        }
                }
                if (i < NMONITORS) {
                        break;
                }
        }

        fprintf(stderr, "destroying monitors\n");
        for (i = 0; i < NMONITORS; i++) {
                medusa_monitor_destroy(monitor[i]);
        }
        return 0;
bail:   for (i = 0; i < NMONITORS; i++) {
                if (monitor[i] != NULL) {
                        medusa_monitor_destroy(monitor[i]);
                }
        }
        return -1;
}

static void sigalarm_handler (int sig)
{
        (void) sig;
        abort();
}

static void sigint_handler (int sig)
{
        (void) sig;
        abort();
}

static int g_do_nothing_thread_running = 1;
static int g_do_nothing_thread_stopped = 0;
static void * do_nothing_thread (void *context)
{
        (void) context;

        while (g_do_nothing_thread_running) {
                usleep(100000);
        }

        g_do_nothing_thread_stopped = 1;
        return NULL;
}

int main (int argc, char *argv[])
{
        int rc;
        unsigned int i;

        pthread_t thread;
        pthread_attr_t thread_attr;

        (void) argc;
        (void) argv;

        pthread_attr_init(&thread_attr);
        pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&thread, &thread_attr, do_nothing_thread, NULL);

        srand(time(NULL));
        signal(SIGALRM, sigalarm_handler);
        signal(SIGINT, sigint_handler);

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                alarm(5);
                fprintf(stderr, "testing poll: %d\n", g_polls[i]);
                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        return -1;
                }
        }

        g_do_nothing_thread_running = 0;
        while (g_do_nothing_thread_stopped == 0) {
                usleep(100000);
        }
        pthread_attr_destroy(&thread_attr);
        return 0;
}
