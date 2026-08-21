
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/epoll.h>

#define MEDUSA_DEBUG_NAME       "poll-epoll"

#include "debug.h"
#include "queue.h"
#include "subject-struct.h"
#include "io.h"
#include "io-private.h"
#include "io-struct.h"

#include "poll-backend.h"
#include "poll-epoll.h"

#define EVENTS_SIZE     (32)
#define EVENTS_STEP     (32)
#define EVENTS_MAX      (8 * 1024)

#define MAX(a, b)       (((a) > (b)) ? (a) : (b))

struct internal {
        struct medusa_poll_backend backend;
        int fd;
        int maxevents;
        struct epoll_event *events;
        struct medusa_io **ios;
        int nios;
        int (*onevent) (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
};

static int internal_ios_grow (struct internal *internal, int fd)
{
        int nios;
        struct medusa_io **tmp;
        if (fd + 1 <= internal->nios) {
                return 0;
        }
        nios = MAX(fd + 1, internal->nios + 64);
        tmp = (struct medusa_io **) realloc(internal->ios, sizeof(struct medusa_io *) * nios);
        if (tmp == NULL) {
                tmp = (struct medusa_io **) malloc(sizeof(struct medusa_io *) * nios);
                if (tmp == NULL) {
                        goto bail;
                }
                if (internal->nios > 0) {
                        memcpy(tmp, internal->ios, sizeof(struct medusa_io *) * internal->nios);
                }
                free(internal->ios);
        }
        memset(&tmp[internal->nios], 0, sizeof(struct medusa_io *) * (nios - internal->nios));
        internal->ios = tmp;
        internal->nios = nios;
        return 0;
bail:   return -1;
}

static int internal_add (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int rc;
        unsigned int events;
        struct epoll_event ev;
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
        rc = internal_ios_grow(internal, io->fd);
        if (rc < 0) {
                medusa_errorf("internal_ios_grow failed, rc: %d", rc);
                goto bail;
        }
        ev.events = 0;
        if (events & MEDUSA_IO_EVENT_IN) {
                ev.events |= EPOLLIN;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                ev.events |= EPOLLOUT;
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                ev.events |= EPOLLPRI;
        }
        ev.data.fd = io->fd;
        rc = epoll_ctl(internal->fd, EPOLL_CTL_ADD, io->fd, &ev);
        if (rc < 0) {
                return -errno;
        }
        internal->ios[io->fd] = io;
        return 0;
bail:   return -1;
}

static int internal_mod (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int rc;
        unsigned int events;
        struct epoll_event ev;
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
        rc = internal_ios_grow(internal, io->fd);
        if (rc < 0) {
                medusa_errorf("internal_ios_grow failed, rc: %d", rc);
                goto bail;
        }
        ev.events = 0;
        if (events & MEDUSA_IO_EVENT_IN) {
                ev.events |= EPOLLIN;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                ev.events |= EPOLLOUT;
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                ev.events |= EPOLLPRI;
        }
        ev.data.fd = io->fd;
        rc = epoll_ctl(internal->fd, EPOLL_CTL_MOD, io->fd, &ev);
        if (rc < 0) {
                return -errno;
        }
        internal->ios[io->fd] = io;
        return 0;
bail:   return -1;
}

static int internal_del (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int rc;
        struct epoll_event ev;
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
        ev.events = 0;
        ev.data.fd = io->fd;
        if (io->fd < internal->nios &&
            internal->ios[io->fd] == io) {
                internal->ios[io->fd] = NULL;
        }
        rc = epoll_ctl(internal->fd, EPOLL_CTL_DEL, io->fd, &ev);
        if (rc < 0) {
                return -errno;
        }
        return 0;
bail:   return -1;
}

static int internal_run (struct medusa_poll_backend *backend, struct timespec *timespec)
{
        int i;
        int rc;
        int count;
        int timeout;
        unsigned int events;
        struct epoll_event *ev;
        struct medusa_io *io;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (timespec == NULL) {
                timeout = -1;
        } else {
                timeout = timespec->tv_sec * 1000 + timespec->tv_nsec / 1000000;
        }
        count = epoll_wait(internal->fd, internal->events, internal->maxevents, timeout);
        if (count == 0) {
                goto out;
        }
        if (count < 0) {
                if (errno == EINTR) {
                        return 0;
                }
                return -errno;
        }
        for (i = 0; i < count; i++) {
                ev = &internal->events[i];
                if (ev->data.fd < 0 ||
                    ev->data.fd >= internal->nios) {
                        medusa_errorf("io fd: %d is invalid", ev->data.fd);
                        continue;
                }
                io = internal->ios[ev->data.fd];
                if (io == NULL) {
                        medusa_errorf("io fd: %d is already destroyed", ev->data.fd);
                        continue;
                }
                events = 0;
                if (ev->events & EPOLLIN) {
                        events |= MEDUSA_IO_EVENT_IN;
                }
                if (ev->events & EPOLLOUT) {
                        events |= MEDUSA_IO_EVENT_OUT;
                }
                if (ev->events & EPOLLPRI) {
                        events |= MEDUSA_IO_EVENT_PRI;
                }
                if (ev->events & EPOLLHUP) {
                        events |= MEDUSA_IO_EVENT_HUP;
                }
                if (ev->events & EPOLLERR) {
                        events |= MEDUSA_IO_EVENT_ERR;
                }
                rc = internal->onevent(backend, io, events, internal->context, NULL);
                if (rc < 0) {
                        medusa_errorf("internal->onevent failed, rc: %d", rc);
                        return rc;
                }
        }
        if (count == internal->maxevents && internal->maxevents < EVENTS_MAX) {
                void *tmp;
                internal->maxevents += EVENTS_STEP;
                tmp = (struct epoll_event *) realloc(internal->events, sizeof(struct epoll_event) * internal->maxevents);
                if (tmp == NULL) {
                        tmp = (struct epoll_event *) malloc(sizeof(struct epoll_event) * internal->maxevents);
                        if (tmp == NULL) {
                                return -errno;
                        }
                        free(internal->events);
                }
                internal->events = tmp;
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
        if (internal->ios != NULL) {
                free(internal->ios);
        }
        free(internal);
}

struct medusa_poll_backend * medusa_monitor_epoll_create (const struct medusa_monitor_epoll_init_options *options)
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
        internal->onevent = options->onevent;
        internal->context = options->context;
        internal->fd = epoll_create1(0);
        if (internal->fd < 0) {
                goto bail;
        }
        internal->maxevents = EVENTS_SIZE;
        internal->events = (struct epoll_event *) malloc(sizeof(struct epoll_event) * internal->maxevents);
        if (internal->events == NULL) {
                goto bail;
        }
        internal->backend.name    = "epoll";
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
