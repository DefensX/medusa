
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
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
        MEDUSA_MONITOR_POLL_SELECT,
#if defined(__WINDOWS__)
        MEDUSA_MONITOR_POLL_WSAPOLL,
#endif
};

static int io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        unsigned int *ievents = (unsigned int *) context;
        (void) io;
        (void) param;
        *ievents |= events;
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        unsigned int ievents;
        struct medusa_io *io;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        ievents = 0;
        io = medusa_io_create(monitor, STDIN_FILENO, io_onevent, &ievents);
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                goto bail;
        }
        rc = medusa_io_set_events(io, MEDUSA_IO_EVENT_IN);
        if (rc < 0) {
                goto bail;
        }
        rc = medusa_io_set_enabled(io, 1);
        if (rc < 0) {
                goto bail;
        }

        medusa_monitor_destroy(monitor);
        monitor = NULL;

        if (ievents != (MEDUSA_IO_EVENT_DESTROY)) {
                fprintf(stderr, "ievents: 0x%08x is invalid\n", ievents);
                goto bail;
        }

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
                        fprintf(stderr, "  failed\n");
                        return -1;
                }
                fprintf(stderr, "success\n");
        }
        return 0;
}
