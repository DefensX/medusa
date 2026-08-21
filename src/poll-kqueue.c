
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/event.h>

#define MEDUSA_DEBUG_NAME       "poll-kqueue"

#include "debug.h"
#include "queue.h"
#include "subject-struct.h"
#include "io.h"
#include "io-private.h"
#include "io-struct.h"

#include "poll-backend.h"
#include "poll-kqueue.h"

#define MAX(a, b)       (((a) > (b)) ? (a) : (b))

#define EVENTS_SIZE     (32)
#define EVENTS_STEP     (32)
#define EVENTS_MAX      (8 * 1024)

#define IOS_STEP        (64)

/* kqueue keeps one knote per (ident, filter) pair, so an io that wants both
 * read and write interest needs two kevents. the filters that are currently
 * armed for a descriptor are tracked here, indexed by descriptor, because no
 * field can be added to struct medusa_io.
 *
 * EV_ADD / EV_DELETE is used rather than EV_ADD / EV_ENABLE|EV_DISABLE:
 *   - it mirrors EPOLL_CTL_ADD / EPOLL_CTL_DEL of the sibling epoll backend,
 *     so the two backends drop interest with the same semantics.
 *   - EV_DISABLE leaves the knote registered for the lifetime of the io. an io
 *     toggles write interest constantly (a tcpsocket arms it to flush and drops
 *     it when the write buffer empties), so a disabled knote would be kernel
 *     state kept alive for interest that has been dropped.
 *   - EV_DELETE is unambiguous about already queued events, while EV_DISABLE is
 *     documented as only stopping kevent() from returning the event, not as
 *     disabling the filter itself.
 *   - internal_del has to delete anyway, so add / delete is a single mechanism
 *     for both "this filter is no longer wanted" and "this io is going away".
 * EV_ADD is idempotent and EV_DELETE of an unregistered knote fails with
 * ENOENT, which is tolerated, so the tracked state can only ever cost a
 * redundant syscall, never a wrong registration.
 *
 * EV_CLEAR is deliberately not used: every other backend here is level
 * triggered (epoll without EPOLLET, poll, select) and the monitor relies on a
 * still readable descriptor being reported again on the next run. */
#define FILTER_READ     (0x00000001)
#define FILTER_WRITE    (0x00000002)

struct internal {
        struct medusa_poll_backend backend;
        int fd;
        int maxevents;
        struct kevent *events;
        struct medusa_io **ios;
        unsigned int *filters;
        unsigned int *revents;
        int nios;
        int (*onevent) (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
};

static void * internal_grow_array (void *array, size_t size, int nold, int nnew)
{
        void *tmp;
        tmp = realloc(array, size * nnew);
        if (tmp == NULL) {
                tmp = malloc(size * nnew);
                if (tmp == NULL) {
                        return NULL;
                }
                if (nold > 0) {
                        memcpy(tmp, array, size * nold);
                }
                free(array);
        }
        memset((unsigned char *) tmp + (size * nold), 0, size * (nnew - nold));
        return tmp;
}

static int internal_grow (struct internal *internal, int fd)
{
        int nios;
        void *tmp;
        if (fd < 0) {
                goto bail;
        }
        if (fd + 1 <= internal->nios) {
                return 0;
        }
        nios = MAX(fd + 1, internal->nios + IOS_STEP);
        tmp = internal_grow_array(internal->ios, sizeof(struct medusa_io *), internal->nios, nios);
        if (tmp == NULL) {
                goto bail;
        }
        internal->ios = tmp;
        tmp = internal_grow_array(internal->filters, sizeof(unsigned int), internal->nios, nios);
        if (tmp == NULL) {
                goto bail;
        }
        internal->filters = tmp;
        tmp = internal_grow_array(internal->revents, sizeof(unsigned int), internal->nios, nios);
        if (tmp == NULL) {
                goto bail;
        }
        internal->revents = tmp;
        internal->nios = nios;
        return 0;
bail:   return -ENOMEM;
}

static int internal_kevent (struct internal *internal, struct medusa_io *io, int filter, int flags)
{
        int rc;
        struct kevent kev;
        EV_SET(&kev, io->fd, filter, flags, 0, 0, io);
        rc = kevent(internal->fd, &kev, 1, NULL, 0, NULL);
        if (rc < 0) {
                return -errno;
        }
        return 0;
}

/* arm the filters in filters and disarm the ones that are not in it. the
 * additions are done first so that a failure halfway through leaves the
 * descriptor over armed rather than under armed: a spurious level triggered
 * report is harmless, a missing one stalls the io. the tracked state is
 * updated one filter at a time, right after the syscall that changed it, so it
 * always describes what the kernel actually holds. */
static int internal_apply (struct internal *internal, struct medusa_io *io, unsigned int filters)
{
        int rc;
        if ((filters & FILTER_READ) &&
            !(internal->filters[io->fd] & FILTER_READ)) {
                rc = internal_kevent(internal, io, EVFILT_READ, EV_ADD);
                if (rc < 0) {
                        medusa_errorf("internal_kevent failed, rc: %d", rc);
                        goto bail;
                }
                internal->filters[io->fd] |= FILTER_READ;
        }
        if ((filters & FILTER_WRITE) &&
            !(internal->filters[io->fd] & FILTER_WRITE)) {
                rc = internal_kevent(internal, io, EVFILT_WRITE, EV_ADD);
                if (rc < 0) {
                        medusa_errorf("internal_kevent failed, rc: %d", rc);
                        goto bail;
                }
                internal->filters[io->fd] |= FILTER_WRITE;
        }
        if (!(filters & FILTER_READ) &&
            (internal->filters[io->fd] & FILTER_READ)) {
                rc = internal_kevent(internal, io, EVFILT_READ, EV_DELETE);
                if (rc < 0 &&
                    rc != -ENOENT &&
                    rc != -EBADF) {
                        medusa_errorf("internal_kevent failed, rc: %d", rc);
                        goto bail;
                }
                internal->filters[io->fd] &= ~FILTER_READ;
        }
        if (!(filters & FILTER_WRITE) &&
            (internal->filters[io->fd] & FILTER_WRITE)) {
                rc = internal_kevent(internal, io, EVFILT_WRITE, EV_DELETE);
                if (rc < 0 &&
                    rc != -ENOENT &&
                    rc != -EBADF) {
                        medusa_errorf("internal_kevent failed, rc: %d", rc);
                        goto bail;
                }
                internal->filters[io->fd] &= ~FILTER_WRITE;
        }
        return 0;
bail:   return rc;
}

/* MEDUSA_IO_EVENT_PRI has no portable kqueue filter. macOS can ask for
 * NOTE_OOB on EVFILT_READ but that fflag does not exist on the bsds, so it is
 * mapped onto EVFILT_READ exactly the way poll-select.c maps it onto the read
 * set: the descriptor stays registered and readable data still wakes it, but
 * out of band arrival is never reported back as MEDUSA_IO_EVENT_PRI. pri is
 * therefore honoured as interest and downgraded to in on delivery, it is not
 * dropped. */
static unsigned int internal_filters (unsigned int events)
{
        unsigned int filters;
        filters = 0;
        if (events & MEDUSA_IO_EVENT_IN) {
                filters |= FILTER_READ;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                filters |= FILTER_WRITE;
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                filters |= FILTER_READ;
        }
        return filters;
}

static int internal_add (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int rc;
        unsigned int events;
        unsigned int filters;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (io->fd < 0) {
                return -EBADF;
        }
        events = medusa_io_get_events_unlocked(io);
        if (events == 0) {
                goto bail;
        }
        filters = internal_filters(events);
        if (filters == 0) {
                goto bail;
        }
        rc = internal_grow(internal, io->fd);
        if (rc < 0) {
                medusa_errorf("internal_grow failed, rc: %d", rc);
                return rc;
        }
        if (internal->ios[io->fd] != NULL &&
            internal->ios[io->fd] != io) {
                medusa_errorf("io fd: %d is already registered", io->fd);
                return -EEXIST;
        }
        internal->ios[io->fd] = io;
        rc = internal_apply(internal, io, filters);
        if (rc < 0) {
                medusa_errorf("internal_apply failed, rc: %d", rc);
                (void) internal_apply(internal, io, 0);
                internal->ios[io->fd] = NULL;
                return rc;
        }
        return 0;
bail:   return -1;
}

static int internal_mod (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int rc;
        unsigned int events;
        unsigned int filters;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (io->fd < 0) {
                return -EBADF;
        }
        events = medusa_io_get_events_unlocked(io);
        if (events == 0) {
                goto bail;
        }
        filters = internal_filters(events);
        if (filters == 0) {
                goto bail;
        }
        if (io->fd >= internal->nios) {
                goto bail;
        }
        if (internal->ios[io->fd] != io) {
                goto bail;
        }
        rc = internal_apply(internal, io, filters);
        if (rc < 0) {
                medusa_errorf("internal_apply failed, rc: %d", rc);
                return rc;
        }
        return 0;
bail:   return -1;
}

static int internal_del (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int rc;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (io->fd < 0) {
                return -EBADF;
        }
        if (io->fd >= internal->nios) {
                goto bail;
        }
        if (internal->ios[io->fd] != io) {
                goto bail;
        }
        rc = internal_apply(internal, io, 0);
        /* the table is cleared whatever the syscall said. a failing EV_DELETE
         * means the knote is already gone, so no armed filter can outlive this
         * call, and internal_run must stop resolving this descriptor to an io
         * that the monitor is about to destroy. any mask accumulated by the
         * first pass of internal_run is dropped as well, so a callback that
         * destroys another io from inside the dispatch loop cannot leave a
         * pending event behind for it. */
        internal->filters[io->fd] = 0;
        internal->revents[io->fd] = 0;
        internal->ios[io->fd] = NULL;
        if (rc < 0) {
                medusa_errorf("internal_apply failed, rc: %d", rc);
                return rc;
        }
        return 0;
bail:   return -1;
}

static int internal_run (struct medusa_poll_backend *backend, struct timespec *timespec)
{
        int i;
        int rc;
        int fd;
        int count;
        unsigned int events;
        struct kevent *kev;
        struct medusa_io *io;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        /* kevent takes a const struct timespec * with the same meaning the
         * backend contract gives it, a null pointer blocking forever, so the
         * timeout needs no conversion. the changelist is empty because add,
         * mod and del apply their changes immediately, which keeps a queued
         * change from ever naming a descriptor that has since been closed and
         * reports a rejected registration to whoever asked for it instead of
         * to a later run. */
        count = kevent(internal->fd, NULL, 0, internal->events, internal->maxevents, timespec);
        if (count == 0) {
                goto out;
        }
        if (count < 0) {
                if (errno == EINTR) {
                        return 0;
                }
                return -errno;
        }
        /* first pass, no callback runs here. kqueue reports one event per
         * (ident, filter) pair, so a descriptor that is both readable and
         * writable appears twice in the batch. the two are folded into one
         * mask per descriptor so that each io is dispatched once per run, the
         * way a single pollfd would be. */
        for (i = 0; i < count; i++) {
                kev = &internal->events[i];
                if (kev->ident >= (uintptr_t) internal->nios) {
                        continue;
                }
                fd = (int) kev->ident;
                events = 0;
                if (kev->filter == EVFILT_READ) {
                        events |= MEDUSA_IO_EVENT_IN;
                }
                if (kev->filter == EVFILT_WRITE) {
                        events |= MEDUSA_IO_EVENT_OUT;
                }
                if (kev->flags & EV_ERROR) {
                        /* a rejected changelist entry, data carries the errno.
                         * nothing here submits changes through internal_run so
                         * this is not expected, it is mapped rather than
                         * ignored. */
                        events |= MEDUSA_IO_EVENT_ERR;
                }
                if (kev->flags & EV_EOF) {
                        /* EVFILT_READ raises EV_EOF as soon as the peer's fin
                         * arrives, exactly like WSAPoll reports POLLHUP, and
                         * unread data can still be sitting in the socket
                         * buffer. in is kept set alongside hup, never replaced
                         * by it, so the read path still drains the socket and
                         * classifies the end of stream by reading rather than
                         * by guessing from the hup. on a socket EV_EOF carries
                         * the pending socket error in fflags. */
                        events |= MEDUSA_IO_EVENT_HUP;
                        if (kev->fflags != 0) {
                                events |= MEDUSA_IO_EVENT_ERR;
                        }
                }
                if ((events & MEDUSA_IO_EVENT_HUP) &&
                    !(events & MEDUSA_IO_EVENT_IN) &&
                    (internal->filters[fd] & FILTER_READ)) {
                        events |= MEDUSA_IO_EVENT_IN;
                }
                internal->revents[fd] |= events;
        }
        /* second pass. the io is resolved through the descriptor indexed table
         * and a null entry is skipped, because kevent returned a batch and a
         * callback dispatched from it can destroy the io of a later member of
         * that same batch. the cookie is cross checked as well, which also
         * catches a descriptor that was closed and handed to another io. */
        rc = 0;
        for (i = 0; i < count; i++) {
                kev = &internal->events[i];
                if (kev->ident >= (uintptr_t) internal->nios) {
                        continue;
                }
                fd = (int) kev->ident;
                events = internal->revents[fd];
                if (events == 0) {
                        continue;
                }
                internal->revents[fd] = 0;
                io = internal->ios[fd];
                if (io == NULL) {
                        medusa_errorf("io fd: %d is already destroyed", fd);
                        continue;
                }
                if (kev->udata != NULL &&
                    kev->udata != io) {
                        medusa_errorf("io fd: %d is already replaced", fd);
                        continue;
                }
                rc = internal->onevent(backend, io, events, internal->context, NULL);
                if (rc < 0) {
                        medusa_errorf("internal->onevent failed, rc: %d", rc);
                        break;
                }
        }
        if (rc < 0) {
                for (; i < count; i++) {
                        if (internal->events[i].ident < (uintptr_t) internal->nios) {
                                internal->revents[(int) internal->events[i].ident] = 0;
                        }
                }
                return rc;
        }
        if (count == internal->maxevents &&
            internal->maxevents < EVENTS_MAX) {
                int maxevents;
                void *tmp;
                maxevents = internal->maxevents + EVENTS_STEP;
                tmp = (struct kevent *) realloc(internal->events, sizeof(struct kevent) * maxevents);
                if (tmp == NULL) {
                        tmp = (struct kevent *) malloc(sizeof(struct kevent) * maxevents);
                        if (tmp != NULL) {
                                free(internal->events);
                        }
                }
                if (tmp != NULL) {
                        internal->events = tmp;
                        internal->maxevents = maxevents;
                }
        }
out:    return count;
bail:   return -1;
}

static void internal_destroy (struct medusa_poll_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return;
        }
        if (internal->fd >= 0) {
                close(internal->fd);
        }
        if (internal->events != NULL) {
                free(internal->events);
        }
        if (internal->revents != NULL) {
                free(internal->revents);
        }
        if (internal->filters != NULL) {
                free(internal->filters);
        }
        if (internal->ios != NULL) {
                free(internal->ios);
        }
        free(internal);
}

struct medusa_poll_backend * medusa_monitor_kqueue_create (const struct medusa_monitor_kqueue_init_options *options)
{
        struct internal *internal;
        internal = NULL;
        if (options == NULL) {
                goto bail;
        }
        internal = (struct internal *) malloc(sizeof(struct internal));
        if (internal == NULL) {
                goto bail;
        }
        memset(internal, 0, sizeof(struct internal));
        internal->fd = -1;
        internal->onevent = options->onevent;
        internal->context = options->context;
        internal->fd = kqueue();
        if (internal->fd < 0) {
                goto bail;
        }
        internal->maxevents = EVENTS_SIZE;
        internal->events = (struct kevent *) malloc(sizeof(struct kevent) * internal->maxevents);
        if (internal->events == NULL) {
                goto bail;
        }
        internal->backend.name    = "kqueue";
        internal->backend.add     = internal_add;
        internal->backend.mod     = internal_mod;
        internal->backend.del     = internal_del;
        internal->backend.run     = internal_run;
        internal->backend.destroy = internal_destroy;
        return &internal->backend;
bail:   if (internal != NULL) {
                internal_destroy(&internal->backend);
        }
        return NULL;
}
