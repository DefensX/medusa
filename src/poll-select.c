
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#if defined(__WINDOWS__)
#define FD_SETSIZE      8192
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#define MEDUSA_DEBUG_NAME       "poll-select"

#include "debug.h"
#include "queue.h"
#include "subject-struct.h"
#include "io.h"
#include "io-private.h"
#include "io-struct.h"

#include "poll-backend.h"
#include "poll-select.h"

#if defined(__DARWIN__) && (__DARWIN__ == 1)
#define SELECT_FD_SETSIZE       __DARWIN_FD_SETSIZE
#elif defined(__WINDOWS__)
#define SELECT_FD_SETSIZE       FD_SETSIZE
#else
#define SELECT_FD_SETSIZE       __FD_SETSIZE
#endif

#define MAX(a, b)       (((a) > (b)) ? (a) : (b))

struct internal {
        struct medusa_poll_backend backend;
        fd_set rfds;
        fd_set wfds;
        fd_set efds;
        fd_set _rfds;
        fd_set _wfds;
        fd_set _efds;
        struct medusa_io *ios[SELECT_FD_SETSIZE];
        int (*onevent) (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
};

static int internal_add (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        unsigned int events;
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
        if (io->fd >= SELECT_FD_SETSIZE) {
                goto bail;
        }
        FD_CLR(io->fd, &internal->rfds);
        FD_CLR(io->fd, &internal->wfds);
        FD_CLR(io->fd, &internal->efds);
        if (events & MEDUSA_IO_EVENT_IN) {
                FD_SET(io->fd, &internal->rfds);
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                FD_SET(io->fd, &internal->wfds);
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                FD_SET(io->fd, &internal->rfds);
        }
        FD_SET(io->fd, &internal->efds);
        internal->ios[io->fd] = io;
        return 0;
bail:   return -1;
}

static int internal_mod (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        unsigned int events;
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
        if (internal->ios[io->fd] != io) {
                goto bail;
        }
        FD_CLR(io->fd, &internal->rfds);
        FD_CLR(io->fd, &internal->wfds);
        FD_CLR(io->fd, &internal->efds);
        if (events & MEDUSA_IO_EVENT_IN) {
                FD_SET(io->fd, &internal->rfds);
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                FD_SET(io->fd, &internal->wfds);
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                FD_SET(io->fd, &internal->rfds);
        }
        FD_SET(io->fd, &internal->efds);
        return 0;
bail:   return -1;
}

static int internal_del (struct medusa_poll_backend *backend, struct medusa_io *io)
{
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
        if (internal->ios[io->fd] != io) {
                goto bail;
        }
        FD_CLR(io->fd, &internal->rfds);
        FD_CLR(io->fd, &internal->wfds);
        FD_CLR(io->fd, &internal->efds);
        internal->ios[io->fd] = NULL;
        return 0;
bail:   return -1;
}

static int internal_run (struct medusa_poll_backend *backend, struct timespec *timespec)
{
        int i;
        int rc;
        int count;
        unsigned int events;
        struct timeval *timeval;
        struct timeval _timeval;
        struct medusa_io *io;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (timespec != NULL) {
                timeval = &_timeval;
                _timeval.tv_sec = timespec->tv_sec;
                _timeval.tv_usec = timespec->tv_nsec / 1000;
        } else {
                timeval = NULL;
        }
        memcpy(&internal->_rfds, &internal->rfds, sizeof(internal->rfds));
        memcpy(&internal->_wfds, &internal->wfds, sizeof(internal->wfds));
        memcpy(&internal->_efds, &internal->efds, sizeof(internal->efds));
        count = select(SELECT_FD_SETSIZE, &internal->_rfds, &internal->_wfds, &internal->_efds, timeval);
        if (count == 0) {
                goto out;
        }
        if (count < 0) {
                if (errno == EINTR) {
                        goto out;
                }
                goto bail;
        }
        for (i = 0; i < SELECT_FD_SETSIZE; i++) {
                events = 0;
                if (FD_ISSET(i, &internal->_rfds)) {
                        events |= MEDUSA_IO_EVENT_IN;
                }
                if (FD_ISSET(i, &internal->_wfds)) {
                        events |= MEDUSA_IO_EVENT_OUT;
                }
                if (FD_ISSET(i, &internal->_efds)) {
                        events |= MEDUSA_IO_EVENT_ERR;
                }
                if (events == 0) {
                        continue;
                }
                io = internal->ios[i];
                if (io == NULL) {
                        medusa_errorf("io fd: %d is already destroyed", i);
                        continue;
                }
                rc = internal->onevent(backend, io, events, internal->context, NULL);
                if (rc < 0) {
                        medusa_errorf("internal->onevent failed, rc: %d", rc);
                        goto bail;
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
        free(internal);
}

struct medusa_poll_backend * medusa_monitor_select_create (const struct medusa_monitor_select_init_options *options)
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
        internal->backend.name    = "select";
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
