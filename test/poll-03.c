
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
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

/* an io is destroyed and its descriptor number is immediately handed to a new
 * io from inside the dispatch loop. the pending batch entry still names that
 * descriptor number, and it must not be delivered to the new io, which has had
 * nothing written to it. */

#define NIOS    (4)

struct state {
        int done;
        int spurious;
        int newfd;
        struct medusa_io *victim;
        int victim_fd;
        struct medusa_io *fresh;
};

static int fresh_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        struct state *state = context;
        (void) io;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        state->spurious += 1;
        return 0;
}

static int io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        char c;
        int sp[2];
        struct state *state = context;
        struct medusa_monitor *monitor;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        if (!(events & MEDUSA_IO_EVENT_IN)) {
                return 0;
        }
        if (read(medusa_io_get_fd(io), &c, 1) != 1) {
                return -1;
        }
        if (state->done) {
                return 0;
        }
        if (medusa_io_get_fd(io) == state->victim_fd) {
                return 0;
        }
        state->done = 1;
        monitor = medusa_io_get_monitor(io);
        medusa_io_destroy(state->victim);
        state->victim = NULL;
        close(state->victim_fd);
        if (pipe(sp) != 0) {
                return -1;
        }
        state->newfd = sp[0];
        state->fresh = medusa_io_create(monitor, sp[0], fresh_onevent, state);
        if (MEDUSA_IS_ERR_OR_NULL(state->fresh)) {
                return -1;
        }
        if (medusa_io_set_events(state->fresh, MEDUSA_IO_EVENT_IN) < 0) {
                return -1;
        }
        if (medusa_io_set_enabled(state->fresh, 1) < 0) {
                return -1;
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int i;
        int rc;
        int fds[NIOS][2];
        struct state state;
        struct medusa_io *io;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        monitor = NULL;
        memset(&state, 0, sizeof(state));
        for (i = 0; i < NIOS; i++) {
                fds[i][0] = -1;
                fds[i][1] = -1;
        }

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;
        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }
        for (i = 0; i < NIOS; i++) {
                if (pipe(fds[i]) != 0) {
                        goto bail;
                }
                io = medusa_io_create(monitor, fds[i][0], io_onevent, &state);
                if (MEDUSA_IS_ERR_OR_NULL(io)) {
                        goto bail;
                }
                if (medusa_io_set_events(io, MEDUSA_IO_EVENT_IN) < 0) {
                        goto bail;
                }
                if (medusa_io_set_enabled(io, 1) < 0) {
                        goto bail;
                }
                if (i == NIOS - 1) {
                        state.victim = io;
                        state.victim_fd = fds[i][0];
                }
        }
        rc = medusa_monitor_run_timeout(monitor, 0.05);
        if (rc < 0) {
                goto bail;
        }
        for (i = 0; i < NIOS; i++) {
                if (write(fds[i][1], "x", 1) != 1) {
                        goto bail;
                }
        }
        for (i = 0; i < 3; i++) {
                rc = medusa_monitor_run_timeout(monitor, 0.05);
                if (rc < 0) {
                        goto bail;
                }
        }
        if (state.spurious != 0) {
                fprintf(stderr, "poll: %d delivered %d events to an io that reused fd %d\n", poll, state.spurious, state.newfd);
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
        for (i = 0; i < NIOS; i++) {
                if (fds[i][0] >= 0 && fds[i][0] != state.victim_fd) {
                        close(fds[i][0]);
                }
                if (fds[i][1] >= 0) {
                        close(fds[i][1]);
                }
        }
        if (state.newfd > 0) {
                close(state.newfd);
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
