
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

#include "error.h"
#include "pipe.h"
#include "clock.h"
#include "queue.h"
#include "signal.h"
#include "signal-backend.h"
#include "subject-struct.h"
#include "signal-struct.h"
#include "signal-private.h"

#include "signal-sigaction.h"

TAILQ_HEAD(items, item);
struct item {
        TAILQ_ENTRY(item) list;
        struct medusa_signal *signal;
        struct sigaction sa;
};

struct internal {
        struct medusa_signal_backend backend;
        int sfd[2];
        struct items items;
};

static int g_signal_handler_wakeup_write_fd = -1;
static pthread_mutex_t g_signal_handler_wakeup_mutex = PTHREAD_MUTEX_INITIALIZER;

static void internal_signal_handler (int number)
{
        int rc;
        pthread_mutex_lock(&g_signal_handler_wakeup_mutex);
        if (g_signal_handler_wakeup_write_fd >= 0) {
                rc = write(g_signal_handler_wakeup_write_fd, &number, sizeof(int));
                (void) rc;
        }
        pthread_mutex_unlock(&g_signal_handler_wakeup_mutex);
}

static int internal_fd (struct medusa_signal_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (MEDUSA_IS_ERR_OR_NULL(internal)) {
                return -EINVAL;
        }
        return internal->sfd[0];
}

static int internal_add (struct medusa_signal_backend *backend, struct medusa_signal *signal)
{
        int rc;
        struct item *item;
        struct sigaction sa;
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
        pthread_mutex_lock(&g_signal_handler_wakeup_mutex);
        if (g_signal_handler_wakeup_write_fd == -1) {
                g_signal_handler_wakeup_write_fd = internal->sfd[1];
        } else if (g_signal_handler_wakeup_write_fd != internal->sfd[1]) {
                pthread_mutex_unlock(&g_signal_handler_wakeup_mutex);
                return -EBUSY;
        }
        pthread_mutex_unlock(&g_signal_handler_wakeup_mutex);
        item = malloc(sizeof(struct item));
        if (item == NULL) {
                return -ENOMEM;
        }
        item->signal = signal;
        memset(&sa, 0, sizeof(struct sigaction));
        sa.sa_handler = internal_signal_handler;
        rc = sigaction(signal->number, &sa, &item->sa);
        if (rc < 0) {
                free(item);
                return -errno;
        }
        TAILQ_INSERT_TAIL(&internal->items, item, list);
        return 0;
}

static int internal_del (struct medusa_signal_backend *backend, struct medusa_signal *signal)
{
        int rc;
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
        rc = sigaction(signal->number, &item->sa, NULL);
        if (rc < 0) {
                return -errno;
        }
        TAILQ_REMOVE(&internal->items, item, list);
        free(item);
        return 0;
}

static int internal_run (struct medusa_signal_backend *backend)
{
        int rc;
        int number;
        struct item *item;
        struct item *nitem;
        struct internal *internal = (struct internal *) backend;
        if (MEDUSA_IS_ERR_OR_NULL(internal)) {
                return -EINVAL;
        }
        rc = read(internal->sfd[0], &number, sizeof(int));
        if (rc < 0) {
                if (errno == EINTR) {
                        return 0;
                } else {
                        return -errno;
                }
        }
        if (rc != sizeof(int)) {
                return -EIO;
        }
        TAILQ_FOREACH_SAFE(item, &internal->items, list, nitem) {
                if (item->signal->number == (int) number) {
                        rc = medusa_signal_onevent(item->signal, MEDUSA_SIGNAL_EVENT_FIRED, NULL);
                        if (rc < 0) {
                                return rc;
                        }
                }
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
        pthread_mutex_lock(&g_signal_handler_wakeup_mutex);
        if (g_signal_handler_wakeup_write_fd == internal->sfd[1]) {
                g_signal_handler_wakeup_write_fd = -1;
        }
        pthread_mutex_unlock(&g_signal_handler_wakeup_mutex);
        g_signal_handler_wakeup_write_fd = -1;
        if (internal->sfd[0] >= 0) {
                close(internal->sfd[0]);
        }
        if (internal->sfd[1] >= 0) {
                close(internal->sfd[1]);
        }
        TAILQ_FOREACH_SAFE(item, &internal->items, list, nitem) {
                sigaction(item->signal->number, &item->sa, NULL);
                TAILQ_REMOVE(&internal->items, item, list);
                free(item);
        }
        free(internal);
}

struct medusa_signal_backend * medusa_signal_sigaction_create (const struct medusa_signal_sigaction_init_options *options)
{
        int rc;
        struct internal *internal;
        (void) options;
        pthread_mutex_lock(&g_signal_handler_wakeup_mutex);
        if (g_signal_handler_wakeup_write_fd != -1) {
                pthread_mutex_unlock(&g_signal_handler_wakeup_mutex);
                return MEDUSA_ERR_PTR(-EALREADY);
        }
        internal = (struct internal *) malloc(sizeof(struct internal));
        if (internal == NULL) {
                pthread_mutex_unlock(&g_signal_handler_wakeup_mutex);
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(internal, 0, sizeof(struct internal));
        TAILQ_INIT(&internal->items);
        rc = medusa_pipe(internal->sfd);
        if (rc < 0) {
                pthread_mutex_unlock(&g_signal_handler_wakeup_mutex);
                internal_destroy(&internal->backend);
                return MEDUSA_ERR_PTR(-errno);
        }
        internal->backend.name    = "sigaction";
        internal->backend.fd      = internal_fd;
        internal->backend.add     = internal_add;
        internal->backend.del     = internal_del;
        internal->backend.run     = internal_run;
        internal->backend.destroy = internal_destroy;
        g_signal_handler_wakeup_write_fd = internal->sfd[1];
        pthread_mutex_unlock(&g_signal_handler_wakeup_mutex);
        return &internal->backend;
}
