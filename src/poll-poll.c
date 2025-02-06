
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#define MEDUSA_DEBUG_NAME       "poll-poll"

#include "debug.h"
#include "queue.h"
#include "subject-struct.h"
#include "io.h"
#include "io-private.h"
#include "io-struct.h"

#include "poll-backend.h"
#include "poll-poll.h"

#define MAX(a, b)       (((a) > (b)) ? (a) : (b))

struct internal {
        struct medusa_poll_backend backend;
        struct pollfd *pfds;
        int npfds;
        int spfds;
        struct medusa_io **ios;
        int nios;
        int (*onevent) (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
};

static int internal_add (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        unsigned int events;
        struct pollfd *pfd;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (io->fd < 0) {
                goto bail;
        }
        events = medusa_io_get_events_unlocked(io);
        if (events == 0) {
                goto bail;
        }
        if (internal->npfds + 1 >= internal->spfds) {
                struct pollfd *tmp;
                tmp = (struct pollfd *) realloc(internal->pfds, sizeof(struct pollfd) * (internal->spfds + 64));
                if (tmp == NULL) {
                        tmp = (struct pollfd *) malloc(sizeof(struct pollfd) * (internal->spfds + 64));
                        if (tmp == NULL) {
                                goto bail;
                        }
                        memcpy(tmp, internal->pfds, sizeof(struct pollfd) * internal->npfds);
                        free(internal->pfds);
                }
                internal->pfds = tmp;
                internal->spfds = internal->spfds + 64;
        }
        if (io->fd + 1 > internal->nios) {
                struct medusa_io **tmp;
                tmp = (struct medusa_io **) realloc(internal->ios, sizeof(struct medusa_io *) * MAX(io->fd + 1, internal->nios + 64));
                if (tmp == NULL) {
                        tmp = (struct medusa_io **) malloc(sizeof(struct medusa_io *) * MAX(io->fd + 1, internal->nios + 64));
                        if (tmp == NULL) {
                                goto bail;
                        }
                        memcpy(tmp, internal->ios, sizeof(struct medusa_io **) * internal->nios);
                        free(internal->ios);
                }
                internal->ios = tmp;
                internal->nios = MAX(io->fd + 1, internal->nios + 64);
        }
        pfd = &internal->pfds[internal->npfds];
        pfd->events = 0;
        if (events & MEDUSA_IO_EVENT_IN) {
                pfd->events |= POLLIN;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                pfd->events |= POLLOUT;
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                pfd->events |= POLLPRI;
        }
        pfd->fd = io->fd;
        internal->ios[io->fd] = io;
        internal->npfds += 1;
        return 0;
bail:   return -1;
}

static int internal_mod (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int i;
        unsigned int events;
        struct pollfd *pfd;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (io->fd < 0) {
                goto bail;
        }
        events = medusa_io_get_events_unlocked(io);
        if (events == 0) {
                goto bail;
        }
        for (i = 0; i < internal->npfds; i++) {
                if (io->fd == internal->pfds[i].fd) {
                        break;
                }
        }
        if (i >= internal->npfds) {
                goto bail;
        }
        pfd = &internal->pfds[i];
        pfd->events = 0;
        if (events & MEDUSA_IO_EVENT_IN) {
                pfd->events |= POLLIN;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                pfd->events |= POLLOUT;
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                pfd->events |= POLLPRI;
        }
        return 0;
bail:   return -1;
}

static int internal_del (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int i;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (io->fd < 0) {
                goto bail;
        }
        for (i = 0; i < internal->npfds; i++) {
                if (io->fd == internal->pfds[i].fd) {
                        break;
                }
        }
        if (i >= internal->npfds) {
                goto bail;
        }
        memmove(&internal->pfds[i], &internal->pfds[i + 1], sizeof(struct pollfd) * (internal->npfds - i - 1));
        internal->npfds -= 1;
        internal->ios[io->fd] = NULL;
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
        for (i = 0; i < internal->npfds; i++) {
                internal->pfds[i].revents = 0;
        }
        count = poll(internal->pfds, internal->npfds, timeout);
        if (count == 0) {
                return 0;
        }
        if (count < 0) {
                if (errno == EINTR) {
                        return 0;
                }
                goto bail;
        }
        for (i = 0; i < internal->npfds; i++) {
                if (internal->pfds[i].revents == 0) {
                        continue;
                }
                events = 0;
                if (internal->pfds[i].revents & POLLIN) {
                        events |= MEDUSA_IO_EVENT_IN;
                }
                if (internal->pfds[i].revents & POLLOUT) {
                        events |= MEDUSA_IO_EVENT_OUT;
                }
                if (internal->pfds[i].revents & POLLPRI) {
                        events |= MEDUSA_IO_EVENT_PRI;
                }
                if (internal->pfds[i].revents & POLLHUP) {
                        events |= MEDUSA_IO_EVENT_HUP;
                }
                if (internal->pfds[i].revents & POLLERR) {
                        events |= MEDUSA_IO_EVENT_ERR;
                }
                if (internal->pfds[i].revents & POLLNVAL) {
                        events |= MEDUSA_IO_EVENT_NVAL;
                }
                io = internal->ios[internal->pfds[i].fd];
                rc = internal->onevent(backend, io, events, internal->context, NULL);
                if (rc < 0) {
                        medusa_errorf("internal->onevent failed, rc: %d", rc);
                        goto bail;
                }
        }
        return count;
bail:   return -1;
}

static void internal_destroy (struct medusa_poll_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return;
        }
        if (internal->ios != NULL) {
                free(internal->ios);
        }
        if (internal->pfds != NULL) {
                free(internal->pfds);
        }
        free(internal);
}

struct medusa_poll_backend * medusa_monitor_poll_create (const struct medusa_monitor_poll_init_options *options)
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
        internal->backend.name    = "poll";
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
