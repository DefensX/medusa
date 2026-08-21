
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
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
        int rc;
        int count;
        (void) context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_IN) {
                int *reads = context;
                rc = read(medusa_io_get_fd(io), &count, sizeof(int));
                if (rc != sizeof(int)) {
                        fprintf(stderr, "can not read fd\n");
                        goto bail;
                }
                *reads += 1;
        }
        return 0;
bail:   return -1;
}

static int test_poll (unsigned int poll)
{
        int rc;
        int fds[2];

        int count;
        int writes;
        int reads;

        long int seed;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct medusa_io *io;

        monitor = NULL;

        count = 10;
        writes = 0;
        reads = 0;

        seed = time(NULL);
        srand(seed);

        fprintf(stderr, "seed: %ld\n", seed);
        count = rand() % 10000;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        rc = pipe(fds);
        if (rc != 0) {
                goto bail;
        }
        io = medusa_io_create(monitor, fds[0], io_onevent, &reads);
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                fprintf(stderr, "medusa_io_create failed\n");
                goto bail;
        }
        rc = medusa_io_set_events(io, MEDUSA_IO_EVENT_IN);
        if (rc < 0) {
                fprintf(stderr, "medusa_io_set_events failed\n");
                goto bail;
        }
        rc = medusa_io_set_enabled(io, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_io_set_enabled failed\n");
                goto bail;
        }

        while (1) {
                if (writes != count) {
                        rc = write(fds[1], &count, sizeof(int));
                        if (rc != sizeof(int)) {
                                goto bail;
                        }
                        writes += 1;
                }
                rc = medusa_monitor_run_timeout(monitor, 1.0);
                if (rc < 0) {
                        fprintf(stderr, "medusa_monitor_run_timeout failed\n");
                        goto bail;
                }
                if (reads == count) {
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
