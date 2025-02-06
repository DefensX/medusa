
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <pthread.h>

#define MEDUSA_DEBUG_NAME       "io"

#include "debug.h"
#include "error.h"
#include "pool.h"
#include "queue.h"
#include "monitor.h"
#include "monitor-private.h"

#include "subject-struct.h"
#include "io-struct.h"
#include "io-private.h"

#include "io.h"

#define MEDUSA_IO_EVENT_MASK            0xff
#define MEDUSA_IO_EVENT_SHIFT           0x00

#define MEDUSA_IO_FLAG_MASK             0xff
#define MEDUSA_IO_FLAG_SHIFT            0x18

#define MEDUSA_IO_USE_POOL              1
#if defined(MEDUSA_IO_USE_POOL) && (MEDUSA_IO_USE_POOL == 1)
static struct medusa_pool *g_pool;
#endif

enum {
        MEDUSA_IO_FLAG_NONE              = 0x00000000,
        MEDUSA_IO_FLAG_ENABLED           = 0x00000001,
        MEDUSA_IO_FLAG_CLODESTROY        = 0x00000002
#define MEDUSA_IO_FLAG_NONE              MEDUSA_IO_FLAG_NONE
#define MEDUSA_IO_FLAG_ENABLED           MEDUSA_IO_FLAG_ENABLED
#define MEDUSA_IO_FLAG_CLODESTROY        MEDUSA_IO_FLAG_CLODESTROY
};

static inline void io_set_events (struct medusa_io *io, unsigned int events)
{
        io->flags = (io->flags & ~(MEDUSA_IO_EVENT_MASK << MEDUSA_IO_EVENT_SHIFT)) |
                    ((events & MEDUSA_IO_EVENT_MASK) << MEDUSA_IO_EVENT_SHIFT);
}

static inline void io_add_events (struct medusa_io *io, unsigned int events)
{
        io->flags |= ((events & MEDUSA_IO_EVENT_MASK) << MEDUSA_IO_EVENT_SHIFT);
}

static inline void io_del_events (struct medusa_io *io, unsigned int events)
{
        io->flags &= ~((events & MEDUSA_IO_EVENT_MASK) << MEDUSA_IO_EVENT_SHIFT);
}

static inline unsigned int io_get_events (const struct medusa_io *io)
{
        return (io->flags >> MEDUSA_IO_EVENT_SHIFT) & MEDUSA_IO_EVENT_MASK;
}

static inline void io_add_flag (struct medusa_io *io, unsigned int flag)
{
        io->flags |= ((flag & MEDUSA_IO_FLAG_MASK) << MEDUSA_IO_FLAG_SHIFT);
}

static inline void io_del_flag (struct medusa_io *io, unsigned int flag)
{
        io->flags &= ~((flag & MEDUSA_IO_FLAG_MASK) << MEDUSA_IO_FLAG_SHIFT);
}

static inline int io_has_flag (const struct medusa_io *io, unsigned int flag)
{
        return !!(io->flags & ((flag & MEDUSA_IO_FLAG_MASK) << MEDUSA_IO_FLAG_SHIFT));
}

static inline unsigned int io_get_enabled (const struct medusa_io *io)
{
        return io_has_flag(io, MEDUSA_IO_FLAG_ENABLED);
}

static inline void io_set_enabled (struct medusa_io *io, unsigned int enabled)
{
        if (enabled) {
                io_add_flag(io, MEDUSA_IO_FLAG_ENABLED);
        } else {
                io_del_flag(io, MEDUSA_IO_FLAG_ENABLED);
        }
}

static inline unsigned int io_get_clodestroy (const struct medusa_io *io)
{
        return io_has_flag(io, MEDUSA_IO_FLAG_CLODESTROY);
}

static inline void io_set_clodestroy (struct medusa_io *io, unsigned int clodestroy)
{
        if (clodestroy) {
                io_add_flag(io, MEDUSA_IO_FLAG_CLODESTROY);
        } else {
                io_del_flag(io, MEDUSA_IO_FLAG_CLODESTROY);
        }
}

static int io_init_with_options_unlocked (struct medusa_io *io, const struct medusa_io_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        if (options->fd < 0) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return -EINVAL;
        }
        memset(io, 0, sizeof(struct medusa_io));
        io->fd = options->fd;
        io->onevent = options->onevent;
        io->context = options->context;
        io_set_events(io, options->events);
        io_set_enabled(io, !!options->enabled);
        io_set_clodestroy(io, !!options->clodestroy);
        medusa_subject_set_type(&io->subject, MEDUSA_SUBJECT_TYPE_IO);
        io->subject.monitor = NULL;
        return medusa_monitor_add_unlocked(options->monitor, &io->subject);
}

static void io_uninit_unlocked (struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return;
        }
        if (io->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&io->subject);
        } else {
                medusa_io_onevent_unlocked(io, MEDUSA_IO_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) int medusa_io_init_options_default (struct medusa_io_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_io_init_options));
        options->fd         = -1;
        options->clodestroy = 0;
        options->enabled    = 0;
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_io * medusa_io_create_unlocked (struct medusa_monitor *monitor, int fd, int (*onevent) (struct medusa_io *io, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_io_init_options options;
        rc = medusa_io_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor = monitor;
        options.fd = fd;
        options.onevent = onevent;
        options.context = context;
        return medusa_io_create_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_io * medusa_io_create (struct medusa_monitor *monitor, int fd, int (*onevent) (struct medusa_io *io, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_io *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_io_create_unlocked(monitor, fd, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_io * medusa_io_create_with_options_unlocked (const struct medusa_io_init_options *options)
{
        int rc;
        struct medusa_io *io;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_IO_USE_POOL) && (MEDUSA_IO_USE_POOL == 1)
        io = medusa_pool_malloc(g_pool);
#else
        io = malloc(sizeof(struct medusa_io));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(io, 0, sizeof(struct medusa_io));
        rc = io_init_with_options_unlocked(io, options);
        if (rc < 0) {
                medusa_io_destroy_unlocked(io);
                return MEDUSA_ERR_PTR(rc);
        }
        return io;
}

__attribute__ ((visibility ("default"))) struct medusa_io * medusa_io_create_with_options (const struct medusa_io_init_options *options)
{
        struct medusa_io *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_io_create_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_io_destroy_unlocked (struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return;
        }
        io_uninit_unlocked(io);
}

__attribute__ ((visibility ("default"))) void medusa_io_destroy (struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return;
        }
        medusa_monitor_lock(io->subject.monitor);
        medusa_io_destroy_unlocked(io);
        medusa_monitor_unlock(io->subject.monitor);
}

__attribute__ ((visibility ("default"))) int medusa_io_get_fd_unlocked (const struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        return io->fd;
}

__attribute__ ((visibility ("default"))) int medusa_io_get_fd (const struct medusa_io *io)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_get_fd_unlocked(io);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_set_events_unlocked (struct medusa_io *io, unsigned int events)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        io_set_events(io, events & (MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_PRI));
        return medusa_monitor_mod_unlocked(&io->subject);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_events (struct medusa_io *io, unsigned int events)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_set_events_unlocked(io, events);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_add_events_unlocked (struct medusa_io *io, unsigned int events)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        events &= MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_PRI;
        if ((io_get_events(io) & events) == events) {
                return 0;
        }
        io_add_events(io, events);
        return medusa_monitor_mod_unlocked(&io->subject);
}

__attribute__ ((visibility ("default"))) int medusa_io_add_events (struct medusa_io *io, unsigned int events)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_add_events_unlocked(io, events);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_del_events_unlocked (struct medusa_io *io, unsigned int events)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        if ((io_get_events(io) & events) == 0) {
                return 0;
        }
        io_del_events(io, events & (MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_PRI));
        return medusa_monitor_mod_unlocked(&io->subject);
}

__attribute__ ((visibility ("default"))) int medusa_io_del_events (struct medusa_io *io, unsigned int events)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_del_events_unlocked(io, events);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) unsigned int medusa_io_get_events_unlocked (const struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return 0;
        }
        return io_get_events(io);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_io_get_events (const struct medusa_io *io)
{
        unsigned int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_get_events_unlocked(io);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_set_enabled_unlocked (struct medusa_io *io, int enabled)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        io_set_enabled(io, !!enabled);
        return medusa_monitor_mod_unlocked(&io->subject);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_enabled (struct medusa_io *io, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_set_enabled_unlocked(io, enabled);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_get_enabled_unlocked (const struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        return io_get_enabled(io);
}

__attribute__ ((visibility ("default"))) int medusa_io_get_enabled (const struct medusa_io *io)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_get_enabled_unlocked(io);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_set_clodestroy_unlocked (struct medusa_io *io, int clodestroy)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        io_set_clodestroy(io, !!clodestroy);
        return medusa_monitor_mod_unlocked(&io->subject);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_clodestroy (struct medusa_io *io, int clodestroy)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_set_clodestroy_unlocked(io, clodestroy);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_get_clodestroy_unlocked (const struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        return io_get_clodestroy(io);
}

__attribute__ ((visibility ("default"))) int medusa_io_get_clodestroy (const struct medusa_io *io)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_get_clodestroy_unlocked(io);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_enable (struct medusa_io *io)
{
        return medusa_io_set_enabled(io, 1);
}

__attribute__ ((visibility ("default"))) int medusa_io_disable (struct medusa_io *io)
{
        return medusa_io_set_enabled(io, 0);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_context_unlocked (struct medusa_io *io, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        io->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_io_set_context (struct medusa_io *io, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_set_context_unlocked(io, context);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_io_get_context_unlocked (struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return io->context;
}

__attribute__ ((visibility ("default"))) void * medusa_io_get_context (struct medusa_io *io)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_get_context_unlocked(io);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_set_userdata_unlocked (struct medusa_io *io, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        io->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_io_set_userdata (struct medusa_io *io, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_set_userdata_unlocked(io, userdata);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_io_get_userdata_unlocked (struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return io->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_io_get_userdata (struct medusa_io *io)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_get_userdata_unlocked(io);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_set_userdata_ptr_unlocked (struct medusa_io *io, void *userdata)
{
        return medusa_io_set_userdata_unlocked(io, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_userdata_ptr (struct medusa_io *io, void *userdata)
{
        return medusa_io_set_userdata(io, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_io_get_userdata_ptr_unlocked (struct medusa_io *io)
{
        return medusa_io_get_userdata_unlocked(io);
}

__attribute__ ((visibility ("default"))) void * medusa_io_get_userdata_ptr (struct medusa_io *io)
{
        return medusa_io_get_userdata(io);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_userdata_int_unlocked (struct medusa_io *io, int userdata)
{
        return medusa_io_set_userdata_unlocked(io, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_userdata_int (struct medusa_io *io, int userdata)
{
        return medusa_io_set_userdata(io, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_io_get_userdata_int_unlocked (struct medusa_io *io)
{
        return (int) (intptr_t) medusa_io_get_userdata_unlocked(io);
}

__attribute__ ((visibility ("default"))) int medusa_io_get_userdata_int (struct medusa_io *io)
{
        return (int) (intptr_t) medusa_io_get_userdata(io);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_userdata_uint_unlocked (struct medusa_io *io, unsigned int userdata)
{
        return medusa_io_set_userdata_unlocked(io, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_io_set_userdata_uint (struct medusa_io *io, unsigned int userdata)
{
        return medusa_io_set_userdata(io, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_io_get_userdata_uint_unlocked (struct medusa_io *io)
{
        return (unsigned int) (intptr_t) medusa_io_get_userdata_unlocked(io);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_io_get_userdata_uint (struct medusa_io *io)
{
        return (unsigned int) (uintptr_t) medusa_io_get_userdata(io);
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_io_get_monitor_unlocked (const struct medusa_io *io)
{
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return io->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_io_get_monitor (const struct medusa_io *io)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_get_monitor_unlocked(io);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_onevent_unlocked (struct medusa_io *io, unsigned int events, void *param)
{
        int rc;
        struct medusa_monitor *monitor;
        rc = 0;
        monitor = io->subject.monitor;
        if (io->onevent != NULL) {
                if ((medusa_subject_is_active(&io->subject)) ||
                    (events & MEDUSA_IO_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        rc = io->onevent(io, events, io->context, param);
                        if (rc < 0) {
                                medusa_errorf("io->onevent failed, ret: %d", rc);
                        }
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                if (io_get_clodestroy(io)) {
                        close(io->fd);
                }
#if defined(MEDUSA_IO_USE_POOL) && (MEDUSA_IO_USE_POOL == 1)
                medusa_pool_free(io);
#else
                free(io);
#endif
        }
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_onevent (struct medusa_io *io, unsigned int events, void *param)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                return -EINVAL;
        }
        medusa_monitor_lock(io->subject.monitor);
        rc = medusa_io_onevent_unlocked(io, events, param);
        medusa_monitor_unlock(io->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_io_is_valid_unlocked (const struct medusa_io *io)
{
        if (io->fd < 0) {
                return 0;
        }
        if (io->onevent == NULL) {
                return 0;
        }
        if ((io_get_events(io) & (MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_PRI)) == 0) {
                return 0;
        }
        if (io_get_enabled(io) == 0) {
                return 0;
        }
        return 1;
}

__attribute__ ((visibility ("default"))) const char * medusa_io_event_string (unsigned int events)
{
        if (events == MEDUSA_IO_EVENT_NONE)     return "MEDUSA_IO_EVENT_NONE";
        if (events == MEDUSA_IO_EVENT_IN)       return "MEDUSA_IO_EVENT_IN";
        if (events == MEDUSA_IO_EVENT_OUT)      return "MEDUSA_IO_EVENT_OUT";
        if (events == MEDUSA_IO_EVENT_PRI)      return "MEDUSA_IO_EVENT_PRI";
        if (events == MEDUSA_IO_EVENT_ERR)      return "MEDUSA_IO_EVENT_ERR";
        if (events == MEDUSA_IO_EVENT_HUP)      return "MEDUSA_IO_EVENT_HUP";
        if (events == MEDUSA_IO_EVENT_HUP)      return "MEDUSA_IO_EVENT_HUP";
        if (events == MEDUSA_IO_EVENT_NVAL)     return "MEDUSA_IO_EVENT_NVAL";
        if (events == MEDUSA_IO_EVENT_DESTROY)  return "MEDUSA_IO_EVENT_DESTROY";
        return "MEDUSA_IO_EVENT_UNKNOWN";
}

__attribute__ ((constructor)) static void io_constructor (void)
{
#if defined(MEDUSA_IO_USE_POOL) && (MEDUSA_IO_USE_POOL == 1)
        g_pool = medusa_pool_create("medusa-io", sizeof(struct medusa_io), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void io_destructor (void)
{
#if defined(MEDUSA_IO_USE_POOL) && (MEDUSA_IO_USE_POOL == 1)
        if (g_pool != NULL) {
                medusa_pool_destroy(g_pool);
        }
#endif
}
