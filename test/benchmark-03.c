
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <time.h>
#include <signal.h>

#include "medusa/error.h"
#include "medusa/clock.h"
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

static int *g_pipes;
static unsigned int g_samples;
static unsigned int g_npipes;
static unsigned int g_nactives;
static unsigned int g_nwrites;
static unsigned int g_fired;
static unsigned int g_count;
static unsigned int g_writes;
static unsigned int g_failures;

static struct medusa_io **g_ios;

static int io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        uintptr_t id;
        unsigned int wid;
        unsigned char ch;
        ssize_t n;

        (void) param;

        id = (uintptr_t) context;
        wid = id + 1;

        if (events & MEDUSA_IO_EVENT_IN) {
                n = read(medusa_io_get_fd(io), (char *) &ch, sizeof(ch));
                if (n >= 0) {
                        g_count += 1;
                } else {
                        g_failures++;
                }
                if (g_writes) {
                        if (wid >= g_npipes) {
                                wid -= g_npipes;
                        }
                        n = write(g_pipes[2 * wid + 1], "e", 1);
                        if (n != 1) {
                                g_failures++;
                        }
                        g_writes--;
                        g_fired++;
                }
        }

        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;
        unsigned int i;
        unsigned int j;
        unsigned int space;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct timespec ts;
        struct timespec te;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        for (i = 0; i < g_npipes; i++) {
                g_ios[i] = medusa_io_create(monitor, g_pipes[i * 2], io_onevent, (void *) ((uintptr_t) i));
                if (MEDUSA_IS_ERR_OR_NULL(g_ios[i])) {
                        goto bail;
                }
        }

        for (j = 0; j < g_samples; j++) {
                for (i = 0; i < g_npipes; i++) {
                        rc = 0;
                        if (medusa_io_get_enabled(g_ios[i])) {
                                rc |= medusa_io_set_enabled(g_ios[i], 0);
                        }
                        rc |= medusa_io_set_events(g_ios[i], MEDUSA_IO_EVENT_IN);
                        rc |= medusa_io_set_enabled(g_ios[i], 1);
                        if (rc < 0) {
                                goto bail;
                        }
                }

                rc = medusa_monitor_run_timeout(monitor, 0.0);
                if (rc < 0) {
                        goto bail;
                }

                g_fired = 0;
                space = g_npipes / g_nactives;
                space = space * 2;
                for (i = 0; i < g_nactives; i++, g_fired++) {
                        rc = write(g_pipes[i * space + 1], "e", 1);
                        if (rc != 1) {
                                goto bail;
                        }
                }

                g_count = 0;
                g_writes = g_nwrites;
                {
                        unsigned int xcount = 0;
                        medusa_clock_monotonic(&ts);
                        do {
                                rc = medusa_monitor_run_timeout(monitor, 0.0);
                                if (rc < 0) {
                                        goto bail;
                                }
                                xcount++;
                        } while (g_count != g_fired);
                        medusa_clock_monotonic(&te);
                        medusa_timespec_sub(&te, &ts, &te);
                        if (xcount != g_count) {
                                fprintf(stderr, "xcount: %d, count: %d\n", xcount, g_count);
                        }
                        fprintf(stderr, "%ld\n", te.tv_sec * 1000000 + te.tv_nsec / 1000);
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
        int c;
        int rc;
        unsigned int i;

        srand(time(NULL));
        signal(SIGALRM, alarm_handler);

        g_samples  = 25;
        g_pipes    = NULL;
        g_npipes   = 100;
        g_nactives = 1;
        g_nwrites  = g_npipes;

        while ((c = getopt(argc, argv, "n:a:w:s:")) != -1) {
                switch (c) {
                        case 'n':
                                g_npipes = atoi(optarg);
                                break;
                        case 'a':
                                g_nactives = atoi(optarg);
                                break;
                        case 'w':
                                g_nwrites = atoi(optarg);
                                break;
                        case 's':
                                g_samples = atoi(optarg);
                                break;
                        default:
                                fprintf(stderr, "unknown param: %c", c);
                                return -1;
                }
        }

        g_ios = malloc(sizeof(struct medusa_io *) * g_npipes);
        if (g_ios == NULL) {
                return -1;
        }
        g_pipes = malloc(sizeof(int[2]) * g_npipes);
        if (g_pipes == NULL) {
                return -1;
        }
        for (i = 0; i < g_npipes; i++) {
#if 0
                rc = pipe(&g_pipes[i * 2]);
#else
                rc = socketpair(AF_UNIX, SOCK_STREAM, 0, &g_pipes[i * 2]);
#endif
                if (rc != 0) {
                        return -1;
                }
        }

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                alarm(5);
                fprintf(stderr, "testing poll: %d\n", g_polls[i]);

                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        return -1;
                }
        }

        for (i = 0; i < g_npipes; i++) {
                close(g_pipes[i * 2]);
                close(g_pipes[i * 2 + 1]);
        }
        free(g_pipes);
        free(g_ios);
        return 0;
}
