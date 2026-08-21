
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

/* the first ready io destroys every other one from inside the dispatch loop.
 * the batch the backend is walking still names those descriptors, so it must
 * not deliver anything but destroy to them. */

#define NIOS    (8)

struct entry {
        struct entry *first;
        int idx;
        int dead;
        int leaked;
        struct medusa_io *io;
};

static int io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int i;
        char c;
        struct entry *entry = context;
        struct entry *entries = entry->first;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        if (entry->dead) {
                entries[0].leaked += 1;
                return 0;
        }
        if (!(events & MEDUSA_IO_EVENT_IN)) {
                return 0;
        }
        if (read(medusa_io_get_fd(io), &c, 1) != 1) {
                return -1;
        }
        if (entry->idx != 0) {
                return 0;
        }
        for (i = 1; i < NIOS; i++) {
                if (entries[i].io != NULL) {
                        entries[i].dead = 1;
                        medusa_io_destroy(entries[i].io);
                        entries[i].io = NULL;
                }
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int i;
        int rc;
        int fds[NIOS][2];
        struct entry entries[NIOS];
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        monitor = NULL;
        for (i = 0; i < NIOS; i++) {
                fds[i][0] = -1;
                fds[i][1] = -1;
        }
        memset(entries, 0, sizeof(entries));

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
                entries[i].first = entries;
                entries[i].idx = i;
                entries[i].io = medusa_io_create(monitor, fds[i][0], io_onevent, &entries[i]);
                if (MEDUSA_IS_ERR_OR_NULL(entries[i].io)) {
                        goto bail;
                }
                if (medusa_io_set_events(entries[i].io, MEDUSA_IO_EVENT_IN) < 0) {
                        goto bail;
                }
                if (medusa_io_set_enabled(entries[i].io, 1) < 0) {
                        goto bail;
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
        rc = medusa_monitor_run_timeout(monitor, 1.0);
        if (rc < 0) {
                goto bail;
        }
        if (entries[0].leaked != 0) {
                fprintf(stderr, "poll: %d delivered %d events to destroyed ios\n", poll, entries[0].leaked);
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
        /* the ios do not own the descriptors, so the test must close them:
         * a later backend in the list would otherwise run out, and select is
         * limited to FD_SETSIZE */
        for (i = 0; i < NIOS; i++) {
                if (fds[i][0] >= 0) {
                        close(fds[i][0]);
                }
                if (fds[i][1] >= 0) {
                        close(fds[i][1]);
                }
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
