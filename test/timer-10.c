
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

static struct medusa_monitor *g_monitor;

static int timer_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        int *count;
        (void) timer;
        (void) param;
        count = (int *) context;
        fprintf(stderr, "events: 0x%08x\n", events);
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                *count += 1;
        }
        if (events & MEDUSA_TIMER_EVENT_DESTROY) {
                fprintf(stderr, "break\n");
                return medusa_monitor_break(g_monitor);
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;
        int count;

        struct medusa_timer *timer;
        struct medusa_monitor_init_options options;

        count = 0;
        g_monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        g_monitor = medusa_monitor_create_with_options(&options);
        if (g_monitor == NULL) {
                fprintf(stderr, "medusa_monitor_create failed\n");
                goto bail;
        }

        timer = medusa_timer_create_singleshot(g_monitor, 0.1, timer_onevent, &count);
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                fprintf(stderr, "medusa_timer_create_singleshot failed\n");
                goto bail;
        }

        rc = medusa_monitor_run(g_monitor);
        if (rc != 0) {
                fprintf(stderr, "can not run monitor\n");
                return -1;
        }

        if (count != 1) {
                fprintf(stderr, "error\n");
                return -1;
        }
        fprintf(stderr, "finish\n");

        medusa_monitor_destroy(g_monitor);
        return 0;
bail:   if (g_monitor != NULL) {
                medusa_monitor_destroy(g_monitor);
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
