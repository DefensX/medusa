
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

static int g_ntimeouts;

static int timer_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        (void) param;
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                int *count = context;
                fprintf(stderr, "timeout: %d\n", *count);
                if (*count + 1 == g_ntimeouts) {
                        fprintf(stderr, "break\n");
                        medusa_timer_set_enabled(timer, 0);
                        medusa_monitor_break(medusa_timer_get_monitor(timer));
                }
                *count = *count + 1;
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        int count;
        struct medusa_timer *timer;

        count = 0;
        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                fprintf(stderr, "medusa_monitor_create_with_options failed\n");
                goto bail;
        }

        timer = medusa_timer_create(monitor, timer_onevent, &count);
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                fprintf(stderr, "medusa_timer_create failed\n");
                goto bail;
        }
        rc = medusa_timer_set_initial(timer, 0.2);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_initial failed\n");
                goto bail;
        }
        rc = medusa_timer_set_interval(timer, 0.0001);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_interval failed\n");
                goto bail;
        }
        rc = medusa_timer_set_singleshot(timer, 0);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_singleshot failed\n");
                goto bail;
        }
        rc = medusa_timer_set_resolution(timer, MEDUSA_TIMER_RESOLUTION_NANOSECONDS);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_resolution failed\n");
                goto bail;
        }
        rc = medusa_timer_set_enabled(timer, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_set_enabled failed\n");
                goto bail;
        }

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "medusa_monitor_run failed\n");
                return -1;
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

        g_ntimeouts = 100;

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
