
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

TAILQ_HEAD(entries, entry);
struct entry {
        TAILQ_ENTRY(entry) list;
        struct medusa_signal *signal;
};

struct internal {
        struct medusa_signal_backend backend;
        struct entries entries;
};

static int internal_add (struct medusa_signal_backend *backend, struct medusa_signal *signal)
{
        struct entry *entry;
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
        TAILQ_FOREACH(entry, &internal->entries, list) {
                if (entry->signal->number == signal->number) {
                        return -EEXIST;
                }
        }
        entry = malloc(sizeof(struct entry));
        if (entry == NULL) {
                return -ENOMEM;
        }
        entry->signal = signal;
        TAILQ_INSERT_TAIL(&internal->entries, entry, list);
        return 0;
}

static int internal_del (struct medusa_signal_backend *backend, struct medusa_signal *signal)
{
        struct entry *entry;
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
        TAILQ_FOREACH(entry, &internal->entries, list) {
                if (entry->signal->number == signal->number) {
                        break;
                }
        }
        if (entry == NULL) {
                return -ENOENT;
        }
        TAILQ_REMOVE(&internal->entries, entry, list);
        free(entry);
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
        struct entry *entry;
        struct entry *nentry;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return;
        }
        TAILQ_FOREACH_SAFE(entry, &internal->entries, list, nentry) {
                TAILQ_REMOVE(&internal->entries, entry, list);
                free(entry);
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
        TAILQ_INIT(&internal->entries);
        internal->backend.name    = "null";
        internal->backend.fd      = NULL;
        internal->backend.add     = internal_add;
        internal->backend.del     = internal_del;
        internal->backend.run     = internal_run;
        internal->backend.destroy = internal_destroy;
        return &internal->backend;
}
