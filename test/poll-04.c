
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

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


/* the writer sends a payload and closes. the backend may report the hangup in
 * the same pass, but it must keep reporting the descriptor readable until the
 * payload has actually been drained: a backend that replaces in with hup loses
 * whatever was still buffered. this is the descriptor level form of the
 * truncation seen on windows, where wsapoll raises POLLHUP on the peer's fin.
 */

#define PAYLOAD (32 * 1024)

struct state {
        long int got;
        int eof;
        int hup;
};

static int io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        char buffer[4096];
        struct state *state = context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        if (events & MEDUSA_IO_EVENT_HUP) {
                state->hup += 1;
        }
        if (!(events & MEDUSA_IO_EVENT_IN)) {
                return 0;
        }
        while (1) {
                rc = (int) read(medusa_io_get_fd(io), buffer, sizeof(buffer));
                if (rc > 0) {
                        state->got += rc;
                        continue;
                }
                if (rc == 0) {
                        state->eof = 1;
                }
                break;
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int i;
        int rc;
        int fds[2];
        long int sent;
        char chunk[4096];
        struct state state;
        struct medusa_io *io;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        monitor = NULL;
        memset(&state, 0, sizeof(state));
        memset(chunk, 'a', sizeof(chunk));

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;
        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }
        if (pipe(fds) != 0) {
                goto bail;
        }
        /* both ends non blocking: the drain loop reads until it would block,
         * and the fill loop writes until it would block, so the test does not
         * depend on the pipe capacity */
        if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0) {
                goto bail;
        }
        if (fcntl(fds[1], F_SETFL, O_NONBLOCK) != 0) {
                goto bail;
        }
        io = medusa_io_create(monitor, fds[0], io_onevent, &state);
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                goto bail;
        }
        if (medusa_io_set_events(io, MEDUSA_IO_EVENT_IN) < 0) {
                goto bail;
        }
        if (medusa_io_set_enabled(io, 1) < 0) {
                goto bail;
        }
        /* buffer the whole payload before the monitor ever runs, then hang up,
         * so the data is already pending when the hangup becomes visible.
         * PAYLOAD stays inside the pipe capacity so no write blocks. */
        sent = 0;
        while (sent < PAYLOAD) {
                rc = (int) write(fds[1], chunk, sizeof(chunk));
                if (rc <= 0) {
                        break;
                }
                sent += rc;
        }
        if (sent <= 0) {
                goto bail;
        }
        close(fds[1]);
        for (i = 0; i < 200 && !state.eof; i++) {
                rc = medusa_monitor_run_timeout(monitor, 0.05);
                if (rc < 0) {
                        goto bail;
                }
        }
        if (!state.eof) {
                fprintf(stderr, "poll: %d never reached eof, drained %ld of %ld bytes (hup: %d)\n", poll, state.got, sent, state.hup);
                goto bail;
        }
        if (state.got != sent) {
                fprintf(stderr, "poll: %d drained %ld of %ld bytes after hangup (hup: %d)\n", poll, state.got, sent, state.hup);
                goto bail;
        }
        medusa_monitor_destroy(monitor);
        close(fds[0]);
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
                        fprintf(stderr, "error\n");
                        return -1;
                }
        }
        fprintf(stderr, "success\n");
        return 0;
}
