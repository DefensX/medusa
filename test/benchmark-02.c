
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <time.h>
#include <signal.h>

#include "medusa/error.h"
#include "medusa/io.h"
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
        MEDUSA_MONITOR_POLL_POLL,
        MEDUSA_MONITOR_POLL_SELECT,
#if defined(__WINDOWS__)
        MEDUSA_MONITOR_POLL_WSAPOLL,
#endif
};

static int io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        (void) io;
        (void) events;
        (void) context;
        (void) param;
        return 0;
}

static int timer_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        (void) timer;
        (void) events;
        (void) context;
        (void) param;
        return 0;
}

static int test_poll (unsigned int poll, int *pipes, unsigned int count)
{
        int rc;
        unsigned int i;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct medusa_io *io;
        struct medusa_timer *timer;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        for (i = 0; i < (count + 1) / 2; i++) {
                io = medusa_io_create(monitor, pipes[i * 2], io_onevent, NULL);
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

                timer = medusa_timer_create(monitor, timer_onevent, NULL);
                if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                        goto bail;
                }
                rc = medusa_timer_set_interval(timer, rand());
                if (rc < 0) {
                        goto bail;
                }
                rc = medusa_timer_set_singleshot(timer, rand() % 2);
                if (rc < 0) {
                        goto bail;
                }
                rc = medusa_timer_set_enabled(timer, 1);
                if (rc < 0) {
                        goto bail;
                }
        }

        rc = medusa_monitor_run_timeout(monitor, 0.0);
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

static void alarm_handler (int sig)
{
        (void) sig;
        abort();
}

int main (int argc, char *argv[])
{
        int rc;
        unsigned int i;

        unsigned int count;
        int *pipes;

        (void) argc;
        (void) argv;

        srand(time(NULL));
        signal(SIGALRM, alarm_handler);

        count = 100;
        pipes = malloc(sizeof(int[2]) * count);
        if (pipes == NULL) {
                return -1;
        }
        for (i = 0; i < count; i++) {
                rc = pipe(&pipes[i * 2]);
                if (rc != 0) {
                        return -1;
                }
        }

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                alarm(5);
                fprintf(stderr, "testing poll: %d\n", g_polls[i]);

                rc = test_poll(g_polls[i], pipes, count);
                if (rc != 0) {
                        return -1;
                }
        }

        for (i = 0; i < count; i++) {
                close(pipes[i * 2]);
                close(pipes[i * 2 + 1]);
        }
        free(pipes);
        return 0;
}
