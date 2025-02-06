
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

static unsigned int g_ntimers;

static unsigned int g_ntimeouts;
static unsigned int g_ndestroys;

static int timer_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        (void) timer;
        (void) context;
        (void) param;
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                g_ntimeouts += 1;
        }
        if (events & MEDUSA_TIMER_EVENT_DESTROY) {
                g_ndestroys += 1;
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;
        unsigned int i;

        struct medusa_timer *timer;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        g_ntimeouts = 0;
        g_ndestroys = 0;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                fprintf(stderr, "medusa_monitor_create failed\n");
                goto bail;
        }

        for (i = 0; i < g_ntimers; i++) {
                timer = medusa_timer_create(monitor, timer_onevent, NULL);
                if (timer == NULL) {
                        goto bail;
                }
                rc  = medusa_timer_set_singleshot(timer, 1);
                rc |= medusa_timer_set_interval(timer, 0.05);
                rc |= medusa_timer_set_enabled(timer, 1);
                if (rc != 0) {
                        fprintf(stderr, "medusa_timer_create_singleshot failed\n");
                        goto bail;
                }
        }

        while (1) {
                rc = medusa_monitor_run_timeout(monitor, 0.100);
                if (rc < 0) {
                        fprintf(stderr, "can not run monitor\n");
                        return -1;
                }
                if (g_ntimeouts == g_ntimers) {
                        break;
                }
        }

        fprintf(stderr, "finish\n");

        medusa_monitor_destroy(monitor);

        if (g_ntimeouts != g_ntimers) {
                return -1;
        }
        if (g_ndestroys != g_ntimers) {
                return -1;
        }

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

        g_ntimers = 100000;

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
