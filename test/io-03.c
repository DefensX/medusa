
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "medusa/io.h"
#include "medusa/event.h"
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

static int io_callback (struct medusa_io *io, unsigned int events, void *context)
{
        (void) context;
        if (events & MEDUSA_EVENT_IN) {
        }
        if (events & MEDUSA_EVENT_TIMEOUT) {
                return medusa_monitor_break(medusa_io_get_monitor(io));
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;
        int fds[2];

        long int seed;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct medusa_io *io;

        monitor = NULL;

        seed = time(NULL);
        srand(seed);

        fprintf(stderr, "seed: %ld\n", seed);

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create(&options);
        if (monitor == NULL) {
                goto bail;
        }

        rc = pipe(fds);
        if (rc != 0) {
                goto bail;
        }
        io = medusa_io_create(monitor);
        if (io == NULL) {
                goto bail;
        }
        rc = medusa_io_set_fd(io, fds[0]);
        if (rc != 0) {
                goto bail;
        }
        rc = medusa_io_set_events(io, MEDUSA_EVENT_IN);
        if (rc != 0) {
                goto bail;
        }
        rc = medusa_io_set_callback(io, io_callback, NULL);
        if (rc != 0) {
                goto bail;
        }
        rc = medusa_io_set_enabled(io, 1);
        if (rc != 0) {
                goto bail;
        }
        rc = medusa_io_set_timeout(io, 1.0);
        if (rc != 0) {
                goto bail;
        }

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                goto bail;
        }

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return 01;
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

        return 0;

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
