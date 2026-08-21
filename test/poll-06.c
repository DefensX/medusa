
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

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

/* interest is transitioned in and out of read and write while data stays
 * pending, and each phase asserts that only the requested direction is
 * reported. this is aimed at kqueue, which keeps one knote per (descriptor,
 * filter) pair: dropping read interest there means actually deleting the
 * EVFILT_READ knote, and a backend that only ever adds filters keeps reporting
 * a direction that is no longer wanted. the payload is never consumed, so the
 * descriptor stays readable for the whole test. */

struct state {
        int in;
        int out;
};

static int io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        struct state *state = context;
        (void) io;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        if (events & MEDUSA_IO_EVENT_IN) {
                state->in += 1;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                state->out += 1;
        }
        return 0;
}

static int phase (struct medusa_monitor *monitor, struct medusa_io *io, struct state *state, unsigned int events)
{
        int rc;
        rc = medusa_io_set_events(io, events);
        if (rc < 0) {
                return -1;
        }
        /* one pass to let the monitor apply the new interest */
        rc = medusa_monitor_run_timeout(monitor, 0.05);
        if (rc < 0) {
                return -1;
        }
        state->in = 0;
        state->out = 0;
        rc = medusa_monitor_run_timeout(monitor, 0.05);
        if (rc < 0) {
                return -1;
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;
        int sp[2];
        struct state state;
        struct medusa_io *io;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        monitor = NULL;
        sp[0] = -1;
        sp[1] = -1;
        memset(&state, 0, sizeof(state));

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;
        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
                goto bail;
        }
        io = medusa_io_create(monitor, sp[0], io_onevent, &state);
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                goto bail;
        }
        if (medusa_io_set_enabled(io, 1) < 0) {
                goto bail;
        }
        /* pending payload that is never read, so read stays ready throughout */
        if (write(sp[1], "xxxx", 4) != 4) {
                goto bail;
        }

        if (phase(monitor, io, &state, MEDUSA_IO_EVENT_IN) != 0) {
                goto bail;
        }
        if (state.in == 0 || state.out != 0) {
                fprintf(stderr, "poll: %d in only: in %d out %d\n", poll, state.in, state.out);
                goto bail;
        }
        if (phase(monitor, io, &state, MEDUSA_IO_EVENT_OUT) != 0) {
                goto bail;
        }
        if (state.out == 0 || state.in != 0) {
                fprintf(stderr, "poll: %d out only: in %d out %d\n", poll, state.in, state.out);
                goto bail;
        }
        if (phase(monitor, io, &state, MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_OUT) != 0) {
                goto bail;
        }
        if (state.in == 0 || state.out == 0) {
                fprintf(stderr, "poll: %d in and out: in %d out %d\n", poll, state.in, state.out);
                goto bail;
        }
        if (phase(monitor, io, &state, MEDUSA_IO_EVENT_IN) != 0) {
                goto bail;
        }
        if (state.in == 0 || state.out != 0) {
                fprintf(stderr, "poll: %d back to in only: in %d out %d\n", poll, state.in, state.out);
                goto bail;
        }
        medusa_monitor_destroy(monitor);
        monitor = NULL;
        rc = 0;
        goto out;
bail:   rc = -1;
out:    if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        if (sp[0] >= 0) {
                close(sp[0]);
        }
        if (sp[1] >= 0) {
                close(sp[1]);
        }
        return rc;
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
                        fprintf(stderr, "error\n");
                        return -1;
                }
        }
        fprintf(stderr, "success\n");
        return 0;
}
