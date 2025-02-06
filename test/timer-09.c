
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#if defined(__WINDOWS__)
#include <windows.h>
#endif

#include "medusa/error.h"
#include "medusa/timer.h"
#include "medusa/monitor.h"

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
        MEDUSA_MONITOR_POLL_SELECT
};

static int timer_count;
static int timer_set_count;

static struct medusa_timer *timer_tik;
static struct medusa_timer *timer_set;

static int timer_set_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        int rc;
        (void) context;
        (void) param;
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                timer_set_count += 1;
                fprintf(stderr, "timer-set: %p callback tm: %p, count: %d, remaining: %.2f\n", timer, timer_tik, timer_set_count, medusa_timer_get_remaining_time(timer_tik));
                rc = medusa_timer_set_enabled(timer_tik, !medusa_timer_get_enabled(timer_tik));
                if (rc < 0) {
                        fprintf(stderr, "medusa_timer_set_enabled failed\n");
                        goto bail;
                }
                if (timer_set_count == 4) {
                        rc = medusa_monitor_break(medusa_timer_get_monitor(timer));
                        if (rc < 0) {
                                fprintf(stderr, "medusa_monitor_break failed\n");
                                goto bail;
                        }
                }
        }
        return 0;
bail:   return -1;
}

static int timer_tik_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        (void) context;
        (void) param;
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                timer_count += 1;
                fprintf(stderr, "timer-tik: %p callback count: %d, remaining: %.2f\n", timer, timer_count, medusa_timer_get_remaining_time(timer_set));
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        monitor = NULL;

        timer_count = 0;
        timer_set_count = 0;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                fprintf(stderr, "medusa_monitor_create failed\n");
                goto bail;
        }

        timer_tik = medusa_timer_create(monitor, timer_tik_onevent, NULL);
        if (MEDUSA_IS_ERR_OR_NULL(timer_tik)) {
                fprintf(stderr, "medusa_timer_create failed\n");
                goto bail;
        }
        rc = medusa_timer_set_interval(timer_tik, 0.1);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_interval failed\n");
                goto bail;
        }
        rc = medusa_timer_set_enabled(timer_tik, 0);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_enabled failed\n");
                goto bail;
        }

        timer_set = medusa_timer_create(monitor, timer_set_onevent, NULL);
        if (MEDUSA_IS_ERR_OR_NULL(timer_set)) {
                fprintf(stderr, "medusa_timer_create failed\n");
                goto bail;
        }
        rc = medusa_timer_set_interval(timer_set, 0.5);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_interval failed\n");
                goto bail;
        }
        rc = medusa_timer_set_enabled(timer_set, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_enabled failed\n");
                goto bail;
        }

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "can not run monitor\n");
                return -1;
        }

        if (timer_count > timer_set_count * 3) {
                fprintf(stderr, "error\n");
                goto bail;
        }

        fprintf(stderr, "finish\n");

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
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
        unsigned int i;

        (void) argc;
        (void) argv;

#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

        srand(time(NULL));
#if !defined(__WINDOWS__)
        signal(SIGALRM, alarm_handler);
#endif

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
#if !defined(__WINDOWS__)
                alarm(5);
#endif
                fprintf(stderr, "testing poll: %d\n", g_polls[i]);

                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        fprintf(stderr, "failed\n");
                        return -1;
                }
        }
        return 0;
}
