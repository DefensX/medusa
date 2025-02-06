
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "clock.h"
#include "timer-backend.h"

#include "timer-monotonic.h"

struct internal {
        struct medusa_timer_backend backend;
        struct timespec timespec;
        int valid;
};

static int internal_set (struct medusa_timer_backend *backend, struct timespec *timespec)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (timespec == NULL) {
                internal->timespec.tv_sec  = 0;
                internal->timespec.tv_nsec = 0;
                internal->valid = 0;
        } else {
                internal->timespec.tv_sec  = timespec->tv_sec;
                internal->timespec.tv_nsec = timespec->tv_nsec;
                internal->valid = 1;
        }
        return 0;
bail:   return -1;
}

static int internal_get (struct medusa_timer_backend *backend, struct timespec *timespec)
{
        int rc;
        struct timespec now;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (timespec == NULL) {
                goto bail;
        }
        if (internal->valid == 1) {
                rc = medusa_clock_monotonic(&now);
                if (rc < 0) {
                        goto bail;
                }
                timespec->tv_sec  = internal->timespec.tv_sec;
                timespec->tv_nsec = internal->timespec.tv_nsec;
                medusa_timespec_sub(timespec, &now, timespec);
                return 1;
        }
        medusa_timespec_clear(timespec);
        return 0;
bail:   return -1;
}

static void internal_destroy (struct medusa_timer_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return;
        }
        free(internal);
}

struct medusa_timer_backend * medusa_timer_monotonic_create (const struct medusa_timer_monotonic_init_options *options)
{
        struct internal *internal;
        (void) options;
        internal = (struct internal *) malloc(sizeof(struct internal));
        if (internal == NULL) {
                goto bail;
        }
        memset(internal, 0, sizeof(struct internal));
        internal->backend.name    = "monotonic";
        internal->backend.fd      = NULL;
        internal->backend.set     = internal_set;
        internal->backend.get     = internal_get;
        internal->backend.destroy = internal_destroy;
        return &internal->backend;
bail:   if (internal != NULL) {
                internal_destroy(&internal->backend);
        }
        return NULL;
}
