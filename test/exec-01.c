
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/exec.h"
#include "medusa/monitor.h"

#if !defined(__LINUX__)

int main (int argc, char *argv[])
{
        (void) argc;
        (void) argv;
        fprintf(stderr, "not supported\n");
        return 0;
}

#else

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

static int exec_onevent (struct medusa_exec *exec, unsigned int events, void *context, void *param)
{
        (void) exec;
        (void) events;
        (void) context;
        (void) param;
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct medusa_exec *exec;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        exec = medusa_exec_create(monitor, (const char *[]) { "ls", "-al", NULL }, exec_onevent, NULL);
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                goto bail;
        }
        rc = medusa_exec_set_enabled(exec, 1);
        if (rc < 0) {
                goto bail;
        }

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

int main (int argc, char *argv[])
{
        int rc;
        unsigned int i;
        (void) argc;
        (void) argv;
        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        return -1;
                }
        }
        return 0;
}

#endif
