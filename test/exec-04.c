
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/exec.h"
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

static int exec_onevent (struct medusa_exec *exec, unsigned int events, void *context, ...)
{
        int wstatus;
        unsigned int *cevents = (unsigned int *) context;
        (void) exec;
        if (events & MEDUSA_EXEC_EVENT_STARTED) {
                *cevents |= MEDUSA_EXEC_EVENT_STARTED;
        }
        if (events & MEDUSA_EXEC_EVENT_STOPPED) {
                *cevents |= MEDUSA_EXEC_EVENT_STOPPED;
                wstatus = medusa_exec_get_wstatus(exec);
                fprintf(stderr, "wstatus: %d\n", wstatus);
                medusa_exec_destroy(exec);
        }
        if (events & MEDUSA_EXEC_EVENT_DESTROY) {
                *cevents |= MEDUSA_EXEC_EVENT_DESTROY;
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        unsigned int events;
        struct medusa_exec *exec;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        events = 0;
        exec = medusa_exec_create(monitor, (const char *[]) { "ls", "-al", NULL }, exec_onevent, &events);
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                goto bail;
        }
        rc = medusa_exec_set_enabled(exec, 1);
        if (rc < 0) {
                goto bail;
        }

        while (1) {
                rc = medusa_monitor_run_timeout(monitor, 1.0);
                if (rc < 0) {
                        goto bail;
                }
                if (rc == 0) {
                        break;
                }
                if (events == (MEDUSA_EXEC_EVENT_STARTED |
                               MEDUSA_EXEC_EVENT_STOPPED |
                               MEDUSA_EXEC_EVENT_DESTROY)) {
                        break;
                }
        }

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
