#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#define MEDUSA_DEBUG_NAME       "poll-wsapoll"

#include "debug.h"
#include "queue.h"
#include "subject-struct.h"
#include "io.h"
#include "io-private.h"
#include "io-struct.h"

#include "poll-backend.h"
#include "poll-wsapoll.h"

#define MAX(a, b)       (((a) > (b)) ? (a) : (b))

struct internal {
        struct medusa_poll_backend backend;
        WSAPOLLFD *pfds;
        int npfds;
        int spfds;
        WSAPOLLFD *rpfds;
        int nrpfds;
        int srpfds;
        struct medusa_io **ios;
        int nios;
        int (*onevent) (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
};

static int internal_add (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        unsigned int events;
        WSAPOLLFD *pfd;
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
                WSAPOLLFD *tmp;
                tmp = (WSAPOLLFD *) realloc(internal->pfds, sizeof(WSAPOLLFD) * (internal->spfds + 64));
                if (tmp == NULL) {
                        tmp = (WSAPOLLFD *) malloc(sizeof(WSAPOLLFD) * (internal->spfds + 64));
                        if (tmp == NULL) {
                                goto bail;
                        }
                        if (internal->npfds > 0) {
                                memcpy(tmp, internal->pfds, sizeof(WSAPOLLFD) * internal->npfds);
                        }
                        free(internal->pfds);
                }
                internal->pfds = tmp;
                internal->spfds = internal->spfds + 64;
        }
        if (io->fd + 1 > internal->nios) {
                int nios;
                struct medusa_io **tmp;
                nios = MAX(io->fd + 1, internal->nios + 64);
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
        }
        pfd = &internal->pfds[internal->npfds];
        pfd->events = 0;
        if (events & MEDUSA_IO_EVENT_IN) {
                pfd->events |= POLLRDNORM;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                pfd->events |= POLLWRNORM;
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                pfd->events |= POLLRDBAND;
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
        WSAPOLLFD *pfd;
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
                if ((SOCKET) io->fd == internal->pfds[i].fd) {
                        break;
                }
        }
        if (i >= internal->npfds) {
                goto bail;
        }
        pfd = &internal->pfds[i];
        pfd->events = 0;
        if (events & MEDUSA_IO_EVENT_IN) {
                pfd->events |= POLLRDNORM;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                pfd->events |= POLLWRNORM;
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                pfd->events |= POLLRDBAND;
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
                if ((SOCKET) io->fd == internal->pfds[i].fd) {
                        break;
                }
        }
        if (i >= internal->npfds) {
                goto bail;
        }
        memmove(&internal->pfds[i], &internal->pfds[i + 1], sizeof(WSAPOLLFD) * (internal->npfds - i - 1));
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
        count = WSAPoll(internal->pfds, internal->npfds, timeout);
        if (count == 0) {
                return 0;
        }
        if (count < 0) {
                goto bail;
        }
        if (internal->npfds > internal->srpfds) {
                int srpfds;
                WSAPOLLFD *tmp;
                /* the ready set is rebuilt from scratch on every run, so the
                 * old entries are not preserved on the realloc fallback path */
                srpfds = MAX(internal->npfds, internal->srpfds + 64);
                tmp = (WSAPOLLFD *) realloc(internal->rpfds, sizeof(WSAPOLLFD) * srpfds);
                if (tmp == NULL) {
                        tmp = (WSAPOLLFD *) malloc(sizeof(WSAPOLLFD) * srpfds);
                        if (tmp == NULL) {
                                goto bail;
                        }
                        free(internal->rpfds);
                }
                internal->rpfds = tmp;
                internal->srpfds = srpfds;
        }
        internal->nrpfds = 0;
        for (i = 0; i < internal->npfds && internal->nrpfds < internal->srpfds; i++) {
                if (internal->pfds[i].revents == 0) {
                        continue;
                }
                internal->rpfds[internal->nrpfds] = internal->pfds[i];
                internal->nrpfds += 1;
        }
        for (i = 0; i < internal->nrpfds; i++) {
                /* Windows WSAPoll specific behavior for CONNECTING state:
                 * when connect() fails, WSAPoll might set POLLERR and POLLHUP instead of standard write event
                 * for connecting socket. We handle POLLERR manually if it occurs. */

                events = 0;
                if (internal->rpfds[i].revents & POLLRDNORM) {
                        events |= MEDUSA_IO_EVENT_IN;
                }
                if (internal->rpfds[i].revents & POLLWRNORM) {
                        events |= MEDUSA_IO_EVENT_OUT;
                }
                if (internal->rpfds[i].revents & POLLRDBAND) {
                        events |= MEDUSA_IO_EVENT_PRI;
                }
                if (internal->rpfds[i].revents & POLLHUP) {
                        events |= MEDUSA_IO_EVENT_HUP;
                }
                if (internal->rpfds[i].revents & POLLERR) {
                        /* Add OUT event if there's POLLERR so that connecting socket
                         * can capture the error through getsockopt on write event */
                        if (events == 0) {
                                events |= MEDUSA_IO_EVENT_OUT;
                        }
                        events |= MEDUSA_IO_EVENT_ERR;
                }
                if (internal->rpfds[i].revents & POLLNVAL) {
                        events |= MEDUSA_IO_EVENT_NVAL;
                }
                io = internal->ios[internal->rpfds[i].fd];
                if (io == NULL) {
                        medusa_errorf("io fd: %d is already destroyed", (int) internal->rpfds[i].fd);
                        continue;
                }
                if (events) {
                        rc = internal->onevent(backend, io, events, internal->context, NULL);
                        if (rc < 0) {
                                medusa_errorf("internal->onevent failed, rc: %d", rc);
                                goto bail;
                        }
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
        if (internal->rpfds != NULL) {
                free(internal->rpfds);
        }
        if (internal->pfds != NULL) {
                free(internal->pfds);
        }
        free(internal);
}

struct medusa_poll_backend * medusa_monitor_wsapoll_create (const struct medusa_monitor_wsapoll_init_options *options)
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
        internal->backend.name    = "wsapoll";
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
