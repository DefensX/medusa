
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
static struct medusa_timer *g_timer_singlehot;
static struct medusa_timer *g_timer_setinterval;

static int timer_singleshot_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        (void) timer;
        (void) context;
        (void) param;
        fprintf(stderr, "events: 0x%08x\n", events);
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                return medusa_monitor_break(g_monitor);
        }
        return 0;
}

static int timer_setinterval_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        struct medusa_timer *t = context;
        (void) timer;
        (void) param;
        fprintf(stderr, "events: 0x%08x\n", events);
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                medusa_timer_set_interval(t, 0.500);
        }
        if (events & MEDUSA_TIMER_EVENT_DESTROY) {
                return medusa_monitor_break(g_monitor);
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;
        int count;

        struct medusa_monitor_init_options options;

        count = 0;
        g_monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        g_monitor = medusa_monitor_create_with_options(&options);
        if (MEDUSA_IS_ERR_OR_NULL(g_monitor)) {
                fprintf(stderr, "medusa_monitor_create failed\n");
                goto bail;
        }

        g_timer_singlehot = medusa_timer_create(g_monitor, timer_singleshot_onevent, &count);
        if (MEDUSA_IS_ERR_OR_NULL(g_timer_singlehot)) {
                fprintf(stderr, "medusa_timer_create_singleshot failed\n");
                goto bail;
        }
        rc  = medusa_timer_set_interval(g_timer_singlehot, 10.00);
        rc |= medusa_timer_set_singleshot(g_timer_singlehot, 1);
        rc |= medusa_timer_set_enabled(g_timer_singlehot, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_create_singleshot failed\n");
                goto bail;
        }

        g_timer_setinterval = medusa_timer_create(g_monitor, timer_setinterval_onevent, g_timer_singlehot);
        if (MEDUSA_IS_ERR_OR_NULL(g_timer_setinterval)) {
                fprintf(stderr, "medusa_timer_create_singleshot failed\n");
                goto bail;
        }
        rc  = medusa_timer_set_interval(g_timer_setinterval, 0.250);
        rc |= medusa_timer_set_singleshot(g_timer_setinterval, 1);
        rc |= medusa_timer_set_enabled(g_timer_setinterval, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_timer_create_singleshot failed\n");
                goto bail;
        }

        rc = medusa_monitor_run(g_monitor);
        if (rc != 0) {
                fprintf(stderr, "can not run monitor\n");
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
