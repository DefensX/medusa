
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include "medusa/error.h"
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
        MEDUSA_MONITOR_POLL_SELECT,
#if defined(__WINDOWS__)
        MEDUSA_MONITOR_POLL_WSAPOLL,
#endif
};

static void * signaller (void *arg)
{
        int rc;
        struct medusa_condition *condition = (struct medusa_condition *) arg;
        usleep(100000);
        fprintf(stderr, "signalling\n");
        rc = medusa_condition_signal(condition);
        if (rc < 0) {
                fprintf(stderr, "can not signal condition\n");
        }
        return NULL;
}

static int condition_onevent (struct medusa_condition *condition, unsigned int events, void *context, void *param)
{
        (void) context;
        (void) param;
        if (events & MEDUSA_CONDITION_EVENT_SIGNAL) {
                fprintf(stderr, "break\n");
                medusa_monitor_break(medusa_condition_get_monitor(condition));
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;
        pthread_t thread;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct medusa_condition *condition;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        condition = medusa_condition_create(monitor, condition_onevent, NULL);
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                goto bail;
        }
        rc = medusa_condition_set_enabled(condition, 1);
        if (rc < 0) {
                goto bail;
        }

        pthread_create(&thread, NULL, signaller, condition);

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "can not run monitor\n");
                return -1;
        }

        pthread_join(thread, NULL);

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
