
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include "error.h"
#include "clock.h"
#include "queue.h"
#include "signal.h"
#include "signal-backend.h"
#include "subject-struct.h"
#include "signal-struct.h"
#include "signal-private.h"

#include "signal-null.h"

TAILQ_HEAD(items, item);
struct item {
        TAILQ_ENTRY(item) list;
        struct medusa_signal *signal;
};

struct internal {
        struct medusa_signal_backend backend;
        struct items items;
};

static int internal_add (struct medusa_signal_backend *backend, struct medusa_signal *signal)
{
        struct item *item;
        struct internal *internal = (struct internal *) backend;
        if (MEDUSA_IS_ERR_OR_NULL(internal)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        if (signal->number <= 0) {
                return -EINVAL;
        }
        TAILQ_FOREACH(item, &internal->items, list) {
                if (item->signal->number == signal->number) {
                        return -EEXIST;
                }
        }
        item = malloc(sizeof(struct item));
        if (item == NULL) {
                return -ENOMEM;
        }
        item->signal = signal;
        TAILQ_INSERT_TAIL(&internal->items, item, list);
        return 0;
}

static int internal_del (struct medusa_signal_backend *backend, struct medusa_signal *signal)
{
        struct item *item;
        struct internal *internal = (struct internal *) backend;
        if (MEDUSA_IS_ERR_OR_NULL(internal)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        if (signal->number <= 0) {
                return -EINVAL;
        }
        TAILQ_FOREACH(item, &internal->items, list) {
                if (item->signal->number == signal->number) {
                        break;
                }
        }
        if (item == NULL) {
                return -ENOENT;
        }
        TAILQ_REMOVE(&internal->items, item, list);
        free(item);
        return 0;
}

static int internal_run (struct medusa_signal_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (MEDUSA_IS_ERR_OR_NULL(internal)) {
                return -EINVAL;
        }
        return 0;
}

static void internal_destroy (struct medusa_signal_backend *backend)
{
        struct item *item;
        struct item *nitem;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return;
        }
        TAILQ_FOREACH_SAFE(item, &internal->items, list, nitem) {
                TAILQ_REMOVE(&internal->items, item, list);
                free(item);
        }
        free(internal);
}

struct medusa_signal_backend * medusa_signal_null_create (const struct medusa_signal_null_init_options *options)
{
        struct internal *internal;
        (void) options;
        internal = (struct internal *) malloc(sizeof(struct internal));
        if (internal == NULL) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(internal, 0, sizeof(struct internal));
        TAILQ_INIT(&internal->items);
        internal->backend.name    = "null";
        internal->backend.fd      = NULL;
        internal->backend.add     = internal_add;
        internal->backend.del     = internal_del;
        internal->backend.run     = internal_run;
        internal->backend.destroy = internal_destroy;
        return &internal->backend;
}
