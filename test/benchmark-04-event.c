
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <time.h>
#include <signal.h>

#include <event2/event.h>
#include <event2/thread.h>

#include "watchdog.h"

static int g_backend;
static unsigned int g_nloops;
static unsigned int g_nsamples;
static unsigned int g_ntimers;
static unsigned int g_npipes;
static unsigned int g_nactives;
static unsigned int g_nwrites;
static unsigned int g_fired;
static unsigned int g_count;
static unsigned int g_writes;
static unsigned int g_failures;

static int *g_pipes;
static struct event **g_events;

static void io_onevent (evutil_socket_t fd, short events, void *context)
{
        uintptr_t id;
        unsigned int wid;
        unsigned char ch;
        ssize_t n;

        id = (uintptr_t) context;
        wid = id + 1;

        if (events & EV_READ) {
                if (g_ntimers) {
                        struct timeval tv;
                        event_del(g_events[id]);
                        tv.tv_sec  = 10;
                        tv.tv_usec = drand48() * 1e6;
                        event_add(g_events[id], &tv);
                }
                n = read(fd, (char *) &ch, sizeof(ch));
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
}

static int test_poll (unsigned int poll)
{
        int rc;
        unsigned int i;
        unsigned int j;
        unsigned int k;
        unsigned int space;

        struct timeval event_timeval;
        struct event_base *event_base;

        struct timeval create_start;
        struct timeval create_finish;
        struct timeval create_total;
        struct timeval destroy_start;
        struct timeval destroy_finish;
        struct timeval destroy_total;
        struct timeval apply_start;
        struct timeval apply_finish;
        struct timeval apply_total;
        struct timeval run_start;
        struct timeval run_finish;
        struct timeval run_total;

        (void) poll;

        event_base = NULL;
        evthread_use_pthreads();

        timerclear(&create_total);
        timerclear(&destroy_total);
        timerclear(&apply_total);
        timerclear(&run_total);

        timerclear(&create_start);
        timerclear(&destroy_start);
        timerclear(&apply_start);
        timerclear(&run_start);

        timerclear(&create_finish);
        timerclear(&destroy_finish);
        timerclear(&apply_finish);
        timerclear(&run_finish);

        for (j = 0; j < g_nloops; j++) {
                gettimeofday(&create_start, NULL);
                event_base = event_base_new();
                if (event_base == NULL) {
                        goto bail;
                }
                for (i = 0; i < g_npipes; i++) {
                        g_events[i] = event_new(event_base, g_pipes[i * 2], EV_READ | EV_PERSIST, io_onevent, (void *) ((uintptr_t) i));
                        if (g_events[i] == NULL) {
                                goto bail;
                        }
                        if (g_ntimers) {
                                event_timeval.tv_sec  = 10.;
                                event_timeval.tv_usec = drand48() * 1e6;
                                rc = event_add(g_events[i], &event_timeval);
                                if (rc != 0) {
                                        goto bail;
                                }
                        } else {
                                rc = event_add(g_events[i], NULL);
                                if (rc != 0) {
                                        goto bail;
                                }
                        }
                }
                rc = event_base_loop(event_base, EVLOOP_ONCE | EVLOOP_NONBLOCK);
                if (rc < 0) {
                        goto bail;
                }
                gettimeofday(&create_finish, NULL);
                timersub(&create_finish, &create_start, &create_finish);
                timeradd(&create_finish, &create_total, &create_total);

                for (k = 0; k < g_nsamples; k++) {
                        gettimeofday(&apply_start, NULL);
                        for (i = 0; i < g_npipes; i++) {
                                event_del(g_events[i]);
                                if (g_ntimers) {
                                        event_timeval.tv_sec  = 10.;
                                        event_timeval.tv_usec = drand48() * 1e6;
                                        rc = event_add(g_events[i], &event_timeval);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                } else {
                                        rc = event_add(g_events[i], NULL);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                }
                        }
                        rc = event_base_loop(event_base, EVLOOP_ONCE | EVLOOP_NONBLOCK);
                        if (rc < 0) {
                                goto bail;
                        }
                        gettimeofday(&apply_finish, NULL);
                        timersub(&apply_finish, &apply_start, &apply_finish);
                        timeradd(&apply_finish, &apply_total, &apply_total);

                        g_fired = 0;
                        space = g_npipes / g_nactives;
                        space = space * 2;
                        for (i = 0; i < g_nactives; i++, g_fired++) {
                                (void) write(g_pipes[i * space + 1], "e", 1);
                        }

                        g_count = 0;
                        g_writes = g_nwrites;

                        gettimeofday(&run_start, NULL);
                        do {
                                rc = event_base_loop(event_base, EVLOOP_ONCE | EVLOOP_NONBLOCK);
                                if (rc < 0) {
                                        goto bail;
                                }
                        } while (g_count != g_fired);
                        gettimeofday(&run_finish, NULL);
                        timersub(&run_finish, &run_start, &run_finish);
                        timeradd(&run_finish, &run_total, &run_total);

                        fprintf(stderr, "%8ld %8ld %8ld %8ld\n",
                                        create_finish.tv_sec * 1000000 + create_finish.tv_usec,
                                        apply_finish.tv_sec * 1000000 + apply_finish.tv_usec,
                                        run_finish.tv_sec * 1000000 + run_finish.tv_usec,
                                        destroy_finish.tv_sec * 1000000 + destroy_finish.tv_usec);
                }
                gettimeofday(&destroy_start, NULL);
                for (i = 0; i < g_npipes; i++) {
                        event_free(g_events[i]);
                }
                event_base_free(event_base);
                gettimeofday(&destroy_finish, NULL);
                timersub(&destroy_finish, &destroy_start, &destroy_finish);
                timeradd(&destroy_finish, &destroy_total, &destroy_total);
        }

        fprintf(stderr, "%8ld %8ld %8ld %8ld %8ld\n",
                        create_total.tv_sec * 1000000 + create_total.tv_usec,
                        apply_total.tv_sec * 1000000 + apply_total.tv_usec,
                        run_total.tv_sec * 1000000 + run_total.tv_usec,
                        destroy_total.tv_sec * 1000000 + destroy_total.tv_usec,
                        (create_total.tv_sec * 1000000 + create_total.tv_usec) +
                        (apply_total.tv_sec * 1000000 + apply_total.tv_usec) +
                        (run_total.tv_sec * 1000000 + run_total.tv_usec) +
                        (destroy_total.tv_sec * 1000000 + destroy_total.tv_usec));

        fprintf(stderr, "%8ld %8ld %8ld %8ld %8ld\n",
                        (create_total.tv_sec * 1000000 + create_total.tv_usec) / g_nloops,
                        (apply_total.tv_sec * 1000000 + apply_total.tv_usec) / (g_nloops * g_nsamples),
                        (run_total.tv_sec * 1000000 + run_total.tv_usec) / (g_nloops * g_nsamples),
                        (destroy_total.tv_sec * 1000000 + destroy_total.tv_usec) / g_nloops,
                        ((create_total.tv_sec * 1000000 + create_total.tv_usec) / g_nloops) +
                        ((apply_total.tv_sec * 1000000 + apply_total.tv_usec) / (g_nloops * g_nsamples)) +
                        ((run_total.tv_sec * 1000000 + run_total.tv_usec) / (g_nloops * g_nsamples)) +
                        ((destroy_total.tv_sec * 1000000 + destroy_total.tv_usec) / g_nloops));

        return 0;
bail:   if (event_base != NULL) {
                event_base_free(event_base);
        }
        return -1;
}

int main (int argc, char *argv[])
{
        int c;
        int rc;
        unsigned int i;

        srand(time(NULL));

        g_backend  = -1;
        g_nloops   = 10;
        g_nsamples = 10;
        g_pipes    = NULL;
        g_npipes   = 10;
        g_nactives = 2;
        g_nwrites  = g_npipes;
        g_ntimers  = 0;

        while ((c = getopt(argc, argv, "hb:l:s:n:a:w:t:")) != -1) {
                switch (c) {
                        case 'b':
                                g_backend = atoi(optarg);
                                break;
                        case 'l':
                                g_nloops = atoi(optarg);
                                break;
                        case 's':
                                g_nsamples = atoi(optarg);
                                break;
                        case 'n':
                                g_npipes = atoi(optarg);
                                break;
                        case 'a':
                                g_nactives = atoi(optarg);
                                break;
                        case 'w':
                                g_nwrites = atoi(optarg);
                                break;
                        case 't':
                                g_ntimers = !!atoi(optarg);
                                break;
                        case 'h':
                                fprintf(stderr, "%s [-b backend] [-l loops] [-s samples] [-n pipes] [-a actives] [-w writes] [-t timers]\n", argv[0]);
                                fprintf(stderr, "  -b: poll backend (default: %d)\n", g_backend);
                                fprintf(stderr, "  -l: loop count (default: %d)\n", g_nloops);
                                fprintf(stderr, "  -s: sample count (default: %d)\n", g_nsamples);
                                fprintf(stderr, "  -n: number of pipes (default: %d)\n", g_npipes);
                                fprintf(stderr, "  -a: number of actives (default: %d)\n", g_nactives);
                                fprintf(stderr, "  -w: number of writes (default: %d)\n", g_nwrites);
                                fprintf(stderr, "  -t: enable timers (default: %d)\n", g_ntimers);
                                return 0;
                        default:
                                fprintf(stderr, "unknown param: %c\n", c);
                                return -1;
                }
        }

        fprintf(stderr, "backend : %d\n", g_backend);
        fprintf(stderr, "loops   : %d\n", g_nloops);
        fprintf(stderr, "samples : %d\n", g_nsamples);
        fprintf(stderr, "pipes   : %d\n", g_npipes);
        fprintf(stderr, "actives : %d\n", g_nactives);
        fprintf(stderr, "writes  : %d\n", g_nwrites);
        fprintf(stderr, "timers  : %d\n", g_ntimers);

        g_events = malloc(sizeof(struct medusa_io *) * g_npipes);
        if (g_events == NULL) {
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
                        fprintf(stderr, "can not create pair: %d\n", i);
                        return -1;
                }
        }

        rc = test_poll(0);
        if (rc != 0) {
                fprintf(stderr, "fail\n");
                return -1;
        }
        fprintf(stderr, "success\n");

        for (i = 0; i < g_npipes; i++) {
                close(g_pipes[i * 2]);
                close(g_pipes[i * 2 + 1]);
        }
        free(g_pipes);
        free(g_events);
        return 0;
}
