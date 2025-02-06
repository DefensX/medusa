
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/timer.h"
#include "medusa/condition.h"
#include "medusa/monitor.h"

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

static int condition1_onevent (struct medusa_condition *condition, unsigned int events, void *context, void *param)
{
        unsigned int *signals = (unsigned int *) context;
        (void) condition;
        (void) param;
        if (events & MEDUSA_CONDITION_EVENT_SIGNAL) {
                *signals |= 1;
        }
        return 0;
}

static int condition2_onevent (struct medusa_condition *condition, unsigned int events, void *context, void *param)
{
        unsigned int *signals = (unsigned int *) context;
        (void) context;
        (void) param;
        if (events & MEDUSA_CONDITION_EVENT_SIGNAL) {
                *signals |= 2;
                if (*signals != (1 | 2)) {
                        fprintf(stderr, "not enough signals\n");
                        return -1;
                }
                fprintf(stderr, "break\n");
                medusa_monitor_break(medusa_condition_get_monitor(condition));
        }
        return 0;
}

static int timer1_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        struct medusa_condition *condition1 = (struct medusa_condition *) context;
        (void) timer;
        (void) param;
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                return medusa_condition_signal(condition1);
        }
        return 0;
}

static int timer2_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        struct medusa_condition *condition2 = (struct medusa_condition *) context;
        (void) timer;
        (void) param;
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                return medusa_condition_signal(condition2);
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        unsigned int signals;

        struct medusa_timer *timer1;
        struct medusa_timer *timer2;

        struct medusa_condition *condition1;
        struct medusa_condition *condition2;

        signals = 0;
        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        condition1 = medusa_condition_create(monitor, condition1_onevent, &signals);
        if (MEDUSA_IS_ERR_OR_NULL(condition1)) {
                goto bail;
        }
        rc = medusa_condition_set_enabled(condition1, 1);
        if (rc < 0) {
                goto bail;
        }

        condition2 = medusa_condition_create(monitor, condition2_onevent, &signals);
        if (MEDUSA_IS_ERR_OR_NULL(condition2)) {
                goto bail;
        }
        rc = medusa_condition_set_enabled(condition2, 1);
        if (rc < 0) {
                goto bail;
        }

        timer1 = medusa_timer_create_singleshot(monitor, 0.1, timer1_onevent, condition1);
        if (MEDUSA_IS_ERR_OR_NULL(timer1)) {
                goto bail;
        }

        timer2 = medusa_timer_create_singleshot(monitor, 0.2, timer2_onevent, condition2);
        if (MEDUSA_IS_ERR_OR_NULL(timer2)) {
                goto bail;
        }

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "can not run monitor\n");
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

static void alarm_handler (int sig)
{
        (void) sig;
        abort();
}

int main (int argc, char *argv[])
{
        int rc;
        unsigned int i;

        (void) argc;
        (void) argv;

        srand(time(NULL));
        signal(SIGALRM, alarm_handler);

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                alarm(5);
                fprintf(stderr, "testing poll: %d\n", g_polls[i]);

                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        return -1;
                }
        }
        return 0;
}
