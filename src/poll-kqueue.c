
#include <stdlib.h>
#include <string.h>

#include "queue.h"
#include "subject-struct.h"
#include "io.h"
#include "io-private.h"
#include "io-struct.h"

#include "poll-backend.h"
#include "poll-kqueue.h"

struct internal {
        struct medusa_poll_backend backend;
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
        return 0;
bail:   return -1;
}

static int internal_run (struct medusa_poll_backend *backend, struct timespec *timespec)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        (void) timespec;
        return -1;
        return 0;
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
        internal->onevent = options->onevent;
        internal->context = options->context;
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
