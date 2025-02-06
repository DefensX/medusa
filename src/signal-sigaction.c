
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

TAILQ_HEAD(entries, entry);
struct entry {
        TAILQ_ENTRY(entry) list;
        struct medusa_signal *signal;
        struct sigaction sa;
};

struct internal {
        struct medusa_signal_backend backend;
        int sfd[2];
        struct entries entries;
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
        struct entry *entry;
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
        TAILQ_FOREACH(entry, &internal->entries, list) {
                if (entry->signal->number == signal->number) {
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
        entry = malloc(sizeof(struct entry));
        if (entry == NULL) {
                return -ENOMEM;
        }
        entry->signal = signal;
        memset(&sa, 0, sizeof(struct sigaction));
        sa.sa_handler = internal_signal_handler;
        rc = sigaction(signal->number, &sa, &entry->sa);
        if (rc < 0) {
                free(entry);
                return -errno;
        }
        TAILQ_INSERT_TAIL(&internal->entries, entry, list);
        return 0;
}

static int internal_del (struct medusa_signal_backend *backend, struct medusa_signal *signal)
{
        int rc;
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
        rc = sigaction(signal->number, &entry->sa, NULL);
        if (rc < 0) {
                return -errno;
        }
        TAILQ_REMOVE(&internal->entries, entry, list);
        free(entry);
        return 0;
}

static int internal_run (struct medusa_signal_backend *backend)
{
        int rc;
        int number;
        struct entry *entry;
        struct entry *nentry;
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
        TAILQ_FOREACH_SAFE(entry, &internal->entries, list, nentry) {
                if (entry->signal->number == (int) number) {
                        rc = medusa_signal_onevent(entry->signal, MEDUSA_SIGNAL_EVENT_FIRED, NULL);
                        if (rc < 0) {
                                return rc;
                        }
                }
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
        TAILQ_FOREACH_SAFE(entry, &internal->entries, list, nentry) {
                sigaction(entry->signal->number, &entry->sa, NULL);
                TAILQ_REMOVE(&internal->entries, entry, list);
                free(entry);
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
        TAILQ_INIT(&internal->entries);
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
