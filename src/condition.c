
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MEDUSA_DEBUG_NAME       "condition"

#include "clock.h"
#include "debug.h"
#include "error.h"
#include "pool.h"
#include "queue.h"
#include "monitor.h"
#include "monitor-private.h"
#include "subject-struct.h"
#include "condition-struct.h"
#include "condition-private.h"

#include "condition.h"

#define MEDUSA_CONDITION_USE_POOL   1
#if defined(MEDUSA_CONDITION_USE_POOL) && (MEDUSA_CONDITION_USE_POOL == 1)
static struct medusa_pool *g_pool;
#endif

enum {
        MEDUSA_CONDITION_FLAG_ENABLED   = 0x00000001,
        MEDUSA_CONDITION_FLAG_SIGNALLED = 0x00000002,
#define MEDUSA_CONDITION_FLAG_ENABLED   MEDUSA_CONDITION_FLAG_ENABLED
#define MEDUSA_CONDITION_FLAG_SIGNALLED MEDUSA_CONDITION_FLAG_SIGNALLED
};

static int condition_set_enabled (struct medusa_condition *condition, int enabled)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        if (enabled) {
                condition->flags |= MEDUSA_CONDITION_FLAG_ENABLED;
                condition->flags &= ~MEDUSA_CONDITION_FLAG_SIGNALLED;
        } else {
                condition->flags &= ~MEDUSA_CONDITION_FLAG_ENABLED;
                condition->flags &= ~MEDUSA_CONDITION_FLAG_SIGNALLED;
        }
        return 0;
}

static int condition_set_signalled (struct medusa_condition *condition, int signalled)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        if (signalled) {
                condition->flags |= MEDUSA_CONDITION_FLAG_SIGNALLED;
        } else {
                condition->flags &= ~MEDUSA_CONDITION_FLAG_SIGNALLED;
        }
        return 0;
}

static int condition_init_with_options_unlocked (struct medusa_condition *condition, const struct medusa_condition_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return -EINVAL;
        }
        memset(condition, 0, sizeof(struct medusa_condition));
        condition->onevent = options->onevent;
        condition->context = options->context;
        condition_set_enabled(condition, options->enabled);
        medusa_subject_set_type(&condition->subject, MEDUSA_SUBJECT_TYPE_CONDITION);
        condition->subject.monitor = NULL;
        return medusa_monitor_add_unlocked(options->monitor, &condition->subject);
}

static void condition_uninit_unlocked (struct medusa_condition *condition)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return;
        }
        if (condition->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&condition->subject);
        } else {
                medusa_condition_onevent_unlocked(condition, MEDUSA_CONDITION_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) int medusa_condition_init_options_default (struct medusa_condition_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_condition_init_options));
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_condition * medusa_condition_create_unlocked (struct medusa_monitor *monitor, int (*onevent) (struct medusa_condition *condition, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_condition_init_options options;
        rc = medusa_condition_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor = monitor;
        options.onevent = onevent;
        options.context = context;
        return medusa_condition_create_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_condition * medusa_condition_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_condition *condition, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_condition *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_condition_create_unlocked(monitor, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_condition * medusa_condition_create_with_options_unlocked (const struct medusa_condition_init_options *options)
{
        int rc;
        struct medusa_condition *condition;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_CONDITION_USE_POOL) && (MEDUSA_CONDITION_USE_POOL == 1)
        condition = medusa_pool_malloc(g_pool);
#else
        condition = malloc(sizeof(struct medusa_condition));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(condition, 0, sizeof(struct medusa_condition));
        rc = condition_init_with_options_unlocked(condition, options);
        if (rc < 0) {
                medusa_condition_destroy_unlocked(condition);
                return MEDUSA_ERR_PTR(rc);
        }
        return condition;
}

__attribute__ ((visibility ("default"))) struct medusa_condition * medusa_condition_create_with_options (const struct medusa_condition_init_options *options)
{
        struct medusa_condition *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_condition_create_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_condition_destroy_unlocked (struct medusa_condition *condition)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return;
        }
        condition_uninit_unlocked(condition);
}

__attribute__ ((visibility ("default"))) void medusa_condition_destroy (struct medusa_condition *condition)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return;
        }
        medusa_monitor_lock(condition->subject.monitor);
        condition_uninit_unlocked(condition);
        medusa_monitor_unlock(condition->subject.monitor);
}

__attribute__ ((visibility ("default"))) int medusa_condition_signal_unlocked (struct medusa_condition *condition)
{
        return medusa_condition_set_signalled_unlocked(condition, 1);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_signalled_unlocked (struct medusa_condition *condition, int signalled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        if (medusa_condition_get_signalled_unlocked(condition) == !!signalled) {
                return 0;
        }
        rc = condition_set_signalled(condition, !!signalled);
        if (rc < 0) {
                return rc;
        }
        return medusa_monitor_mod_unlocked(&condition->subject);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_signalled (struct medusa_condition *condition, int signalled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_set_signalled_unlocked(condition, signalled);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_condition_get_signalled_unlocked (const struct medusa_condition *condition)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        return !!(condition->flags & MEDUSA_CONDITION_FLAG_SIGNALLED);
}

__attribute__ ((visibility ("default"))) int medusa_condition_get_signalled (const struct medusa_condition *condition)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_get_signalled_unlocked(condition);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_condition_signal (struct medusa_condition *condition)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_signal_unlocked(condition);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_enabled_unlocked (struct medusa_condition *condition, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        rc = condition_set_enabled(condition, enabled);
        if (rc < 0) {
                return rc;
        }
        return medusa_monitor_mod_unlocked(&condition->subject);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_enabled (struct medusa_condition *condition, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_set_enabled_unlocked(condition, enabled);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_condition_get_enabled_unlocked (const struct medusa_condition *condition)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        return !!(condition->flags & MEDUSA_CONDITION_FLAG_ENABLED);
}

__attribute__ ((visibility ("default"))) int medusa_condition_get_enabled (const struct medusa_condition *condition)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_get_enabled_unlocked(condition);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_condition_enable (struct medusa_condition *condition)
{
        return medusa_condition_set_enabled(condition, 1);
}

__attribute__ ((visibility ("default"))) int medusa_condition_disable (struct medusa_condition *condition)
{
        return medusa_condition_set_enabled(condition, 0);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_context_unlocked (struct medusa_condition *condition, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        condition->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_context (struct medusa_condition *condition, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_set_context_unlocked(condition, context);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_condition_get_context_unlocked (struct medusa_condition *condition)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return condition->context;
}

__attribute__ ((visibility ("default"))) void * medusa_condition_get_context (struct medusa_condition *condition)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_get_context_unlocked(condition);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_userdata_unlocked (struct medusa_condition *condition, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        condition->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_userdata (struct medusa_condition *condition, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return -EINVAL;
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_set_userdata_unlocked(condition, userdata);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_condition_get_userdata_unlocked (struct medusa_condition *condition)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return condition->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_condition_get_userdata (struct medusa_condition *condition)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_get_userdata_unlocked(condition);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_userdata_ptr_unlocked (struct medusa_condition *condition, void *userdata)
{
        return medusa_condition_set_userdata_unlocked(condition, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_userdata_ptr (struct medusa_condition *condition, void *userdata)
{
        return medusa_condition_set_userdata(condition, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_condition_get_userdata_ptr_unlocked (struct medusa_condition *condition)
{
        return medusa_condition_get_userdata_unlocked(condition);
}

__attribute__ ((visibility ("default"))) void * medusa_condition_get_userdata_ptr (struct medusa_condition *condition)
{
        return medusa_condition_get_userdata(condition);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_userdata_int_unlocked (struct medusa_condition *condition, int userdata)
{
        return medusa_condition_set_userdata_unlocked(condition, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_userdata_int (struct medusa_condition *condition, int userdata)
{
        return medusa_condition_set_userdata(condition, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_condition_get_userdata_int_unlocked (struct medusa_condition *condition)
{
        return (int) (intptr_t) medusa_condition_get_userdata_unlocked(condition);
}

__attribute__ ((visibility ("default"))) int medusa_condition_get_userdata_int (struct medusa_condition *condition)
{
        return (int) (intptr_t) medusa_condition_get_userdata(condition);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_userdata_uint_unlocked (struct medusa_condition *condition, unsigned int userdata)
{
        return medusa_condition_set_userdata_unlocked(condition, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_condition_set_userdata_uint (struct medusa_condition *condition, unsigned int userdata)
{
        return medusa_condition_set_userdata(condition, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_condition_get_userdata_uint_unlocked (struct medusa_condition *condition)
{
        return (unsigned int) (intptr_t) medusa_condition_get_userdata_unlocked(condition);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_condition_get_userdata_uint (struct medusa_condition *condition)
{
        return (unsigned int) (uintptr_t) medusa_condition_get_userdata(condition);
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_condition_get_monitor_unlocked (const struct medusa_condition *condition)
{
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return condition->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_condition_get_monitor (const struct medusa_condition *condition)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(condition)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(condition->subject.monitor);
        rc = medusa_condition_get_monitor_unlocked(condition);
        medusa_monitor_unlock(condition->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_condition_onevent_unlocked (struct medusa_condition *condition, unsigned int events, void *param)
{
        int ret;
        struct medusa_monitor *monitor;
        ret = 0;
        monitor = condition->subject.monitor;
        if (events & MEDUSA_CONDITION_EVENT_SIGNAL) {
                condition_set_signalled(condition, 0);
        }
        if (condition->onevent != NULL) {
                if ((medusa_subject_is_active(&condition->subject)) ||
                    (events & MEDUSA_CONDITION_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        ret = condition->onevent(condition, events, condition->context, param);
                        if (ret < 0) {
                                medusa_errorf("condition->onevent failed, rc: %d", ret);
                        }
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_CONDITION_EVENT_DESTROY) {
#if defined(MEDUSA_CONDITION_USE_POOL) && (MEDUSA_CONDITION_USE_POOL == 1)
                medusa_pool_free(condition);
#else
                free(condition);
#endif
        }
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_condition_is_valid_unlocked (const struct medusa_condition *condition)
{
        if ((condition->flags & MEDUSA_CONDITION_FLAG_ENABLED) == 0) {
                return 0;
        }
        return 1;
}

__attribute__ ((visibility ("default"))) const char * medusa_condition_event_string (unsigned int events)
{
        if (events == MEDUSA_CONDITION_EVENT_SIGNAL)    return "MEDUSA_CONDITION_EVENT_SIGNAL";
        if (events == MEDUSA_CONDITION_EVENT_DESTROY)   return "MEDUSA_CONDITION_EVENT_DESTROY";
        return "MEDUSA_CONDITION_EVENT_UNKNOWN";
}

__attribute__ ((constructor)) static void condition_constructor (void)
{
#if defined(MEDUSA_CONDITION_USE_POOL) && (MEDUSA_CONDITION_USE_POOL == 1)
        g_pool = medusa_pool_create("medusa-condition", sizeof(struct medusa_condition), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void condition_destructor (void)
{
#if defined(MEDUSA_CONDITION_USE_POOL) && (MEDUSA_CONDITION_USE_POOL == 1)
        if (g_pool != NULL) {
                medusa_pool_destroy(g_pool);
        }
#endif
}
