
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/timerfd.h>

#include "clock.h"
#include "timer-backend.h"

#include "timer-timerfd.h"

struct internal {
        struct medusa_timer_backend backend;
        int fd;
        int valid;
};

static int fd_set_blocking (int fd, int on)
{
        int rc;
        int flags;
        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
                return -1;
        }
        flags = on ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
        rc = fcntl(fd, F_SETFL, flags);
        if (rc != 0) {
                return -1;
        }
        return 0;
}

static int internal_fd (struct medusa_timer_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        return internal->fd;
bail:   return -1;
}

static int internal_set (struct medusa_timer_backend *backend, struct timespec *timespec)
{
        int rc;
        struct itimerspec itimerspec;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (timespec == NULL) {
                itimerspec.it_value.tv_sec     = 0;
                itimerspec.it_value.tv_nsec    = 0;
                itimerspec.it_interval.tv_sec  = 0;
                itimerspec.it_interval.tv_nsec = 0;
                internal->valid = 0;
        } else {
                itimerspec.it_value.tv_sec     = timespec->tv_sec;
                itimerspec.it_value.tv_nsec    = timespec->tv_nsec;
                itimerspec.it_interval.tv_sec  = 0;
                itimerspec.it_interval.tv_nsec = 0;
                internal->valid = 1;
        }
        rc = timerfd_settime(internal->fd, TFD_TIMER_ABSTIME, &itimerspec, NULL);
        if (rc != 0) {
                goto bail;
        }
        return 0;
bail:   return -1;
}

static int internal_get (struct medusa_timer_backend *backend, struct timespec *timespec)
{
        int rc;
        struct itimerspec itimerspec;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (timespec == NULL) {
                goto bail;
        }
        rc = timerfd_gettime(internal->fd, &itimerspec);
        if (rc != 0) {
                goto bail;
        }
        timespec->tv_sec  = itimerspec.it_value.tv_sec;
        timespec->tv_nsec = itimerspec.it_value.tv_nsec;
        return internal->valid;
bail:   return -1;
}

static void internal_destroy (struct medusa_timer_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return;
        }
        if (internal->fd >= 0) {
                close(internal->fd);
        }
        free(internal);
}

struct medusa_timer_backend * medusa_timer_timerfd_create (const struct medusa_timer_timerfd_init_options *options)
{
        int rc;
        struct internal *internal;
        (void) options;
        internal = (struct internal *) malloc(sizeof(struct internal));
        if (internal == NULL) {
                goto bail;
        }
        memset(internal, 0, sizeof(struct internal));
        internal->fd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (internal->fd < 0) {
                goto bail;
        }
        rc = fd_set_blocking(internal->fd, 0);
        if (rc != 0) {
                goto bail;
        }
        internal->backend.name    = "timerfd";
        internal->backend.fd      = internal_fd;
        internal->backend.set     = internal_set;
        internal->backend.get     = internal_get;
        internal->backend.destroy = internal_destroy;
        return &internal->backend;
bail:   if (internal != NULL) {
                internal_destroy(&internal->backend);
        }
        return NULL;
}
