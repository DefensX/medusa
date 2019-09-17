
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/io.h"
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

static int io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        (void) io;
        (void) events;
        (void) context;
        (void) param;
        return 0;
}

static int test_poll (unsigned int poll)
{
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct medusa_io *io;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                return -1;
        }

        io = medusa_io_create(monitor, -1, io_onevent, NULL);
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                medusa_monitor_destroy(monitor);
                return 0;
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
