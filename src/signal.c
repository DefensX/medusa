
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MEDUSA_DEBUG_NAME       "signal"

#include "clock.h"
#include "debug.h"
#include "error.h"
#include "pool.h"
#include "queue.h"
#include "monitor.h"
#include "monitor-private.h"
#include "subject-struct.h"
#include "signal-struct.h"
#include "signal-private.h"

#include "signal.h"

#define MEDUSA_SIGNAL_USE_POOL   1
#if defined(MEDUSA_SIGNAL_USE_POOL) && (MEDUSA_SIGNAL_USE_POOL == 1)
static struct medusa_pool *g_pool;
#endif

enum {
        MEDUSA_SIGNAL_FLAG_ENABLED       = 0x00000001,
        MEDUSA_SIGNAL_FLAG_SINGLE_SHOT   = 0x00000002,
        MEDUSA_SIGNAL_FLAG_AUTO_DESTROY  = 0x00000004
#define MEDUSA_SIGNAL_FLAG_ENABLED       MEDUSA_SIGNAL_FLAG_ENABLED
#define MEDUSA_SIGNAL_FLAG_SINGLE_SHOT   MEDUSA_SIGNAL_FLAG_SINGLE_SHOT
#define MEDUSA_SIGNAL_FLAG_AUTO_DESTROY  MEDUSA_SIGNAL_FLAG_AUTO_DESTROY
};

static int signal_set_number (struct medusa_signal *signal, int number)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        if (number < 0) {
                return -EINVAL;
        }
        signal->number = number;
        return 0;
}

static int signal_set_singleshot (struct medusa_signal *signal, int singleshot)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        if (singleshot) {
                signal->flags |= MEDUSA_SIGNAL_FLAG_SINGLE_SHOT;
        } else {
                signal->flags &= ~MEDUSA_SIGNAL_FLAG_SINGLE_SHOT;
        }
        return 0;
}

static int signal_set_enabled (struct medusa_signal *signal, int enabled)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        if (enabled) {
                signal->flags |= MEDUSA_SIGNAL_FLAG_ENABLED;
        } else {
                signal->flags &= ~MEDUSA_SIGNAL_FLAG_ENABLED;
        }
        return 0;
}

static int signal_init_with_options_unlocked (struct medusa_signal *signal, const struct medusa_signal_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        if (options->number <= 0) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return -EINVAL;
        }
        memset(signal, 0, sizeof(struct medusa_signal));
        signal->onevent = options->onevent;
        signal->context = options->context;
        signal_set_number(signal, options->number);
        signal_set_singleshot(signal, options->singleshot);
        signal_set_enabled(signal, options->enabled);
        medusa_subject_set_type(&signal->subject, MEDUSA_SUBJECT_TYPE_SIGNAL);
        signal->subject.monitor = NULL;
        return medusa_monitor_add_unlocked(options->monitor, &signal->subject);
}

static void signal_uninit_unlocked (struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return;
        }
        if (signal->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&signal->subject);
        } else {
                medusa_signal_onevent_unlocked(signal, MEDUSA_SIGNAL_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) int medusa_signal_init_options_default (struct medusa_signal_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_signal_init_options));
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_signal_create_singleshot_unlocked (struct medusa_monitor *monitor, int number, int (*onevent) (struct medusa_signal *signal, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_signal *signal;
        struct medusa_signal_init_options options;
        if (monitor == NULL) {
                return -EINVAL;
        }
        if (number <= 0) {
                return -EINVAL;
        }
        if (onevent == NULL) {
                return -EINVAL;
        }
        rc = medusa_signal_init_options_default(&options);
        if (rc < 0) {
                return rc;
        }
        options.monitor         = monitor;
        options.onevent         = onevent;
        options.context         = context;
        options.number          = number;
        options.singleshot      = 1;
        options.enabled         = 1;
        signal = medusa_signal_create_with_options_unlocked(&options);
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return MEDUSA_PTR_ERR(signal);
        }
        signal->flags |= MEDUSA_SIGNAL_FLAG_AUTO_DESTROY;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_signal_create_singleshot (struct medusa_monitor *monitor, int number, int (*onevent) (struct medusa_signal *signal, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(monitor);
        rc = medusa_signal_create_singleshot_unlocked(monitor, number, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_signal * medusa_signal_create_unlocked (struct medusa_monitor *monitor, int number, int (*onevent) (struct medusa_signal *signal, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_signal_init_options options;
        rc = medusa_signal_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor = monitor;
        options.number = number;
        options.onevent = onevent;
        options.context = context;
        return medusa_signal_create_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_signal * medusa_signal_create (struct medusa_monitor *monitor, int number, int (*onevent) (struct medusa_signal *signal, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_signal *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_signal_create_unlocked(monitor, number, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_signal * medusa_signal_create_with_options_unlocked (const struct medusa_signal_init_options *options)
{
        int rc;
        struct medusa_signal *signal;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_SIGNAL_USE_POOL) && (MEDUSA_SIGNAL_USE_POOL == 1)
        signal = medusa_pool_malloc(g_pool);
#else
        signal = malloc(sizeof(struct medusa_signal));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(signal, 0, sizeof(struct medusa_signal));
        rc = signal_init_with_options_unlocked(signal, options);
        if (rc < 0) {
                medusa_signal_destroy_unlocked(signal);
                return MEDUSA_ERR_PTR(rc);
        }
        return signal;
}

__attribute__ ((visibility ("default"))) struct medusa_signal * medusa_signal_create_with_options (const struct medusa_signal_init_options *options)
{
        struct medusa_signal *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_signal_create_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_signal_destroy_unlocked (struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return;
        }
        signal_uninit_unlocked(signal);
}

__attribute__ ((visibility ("default"))) void medusa_signal_destroy (struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return;
        }
        medusa_monitor_lock(signal->subject.monitor);
        medusa_signal_destroy_unlocked(signal);
        medusa_monitor_unlock(signal->subject.monitor);
}

__attribute__ ((visibility ("default"))) int medusa_signal_get_number_unlocked (const struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        return signal->number;
}

__attribute__ ((visibility ("default"))) int medusa_signal_get_number (const struct medusa_signal *signal)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_get_number_unlocked(signal);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_singleshot_unlocked (struct medusa_signal *signal, int singleshot)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        rc = signal_set_singleshot(signal, singleshot);
        if (rc < 0) {
                return rc;
        }
        return medusa_monitor_mod_unlocked(&signal->subject);
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_singleshot (struct medusa_signal *signal, int singleshot)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_set_singleshot_unlocked(signal, singleshot);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_get_singleshot_unlocked (const struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        return !!(signal->flags & MEDUSA_SIGNAL_FLAG_SINGLE_SHOT);
}

__attribute__ ((visibility ("default"))) int medusa_signal_get_singleshot (const struct medusa_signal *signal)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_get_singleshot_unlocked(signal);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_enabled_unlocked (struct medusa_signal *signal, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        rc = signal_set_enabled(signal, enabled);
        if (rc < 0) {
                return rc;
        }
        return medusa_monitor_mod_unlocked(&signal->subject);
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_enabled (struct medusa_signal *signal, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_set_enabled_unlocked(signal, enabled);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_get_enabled_unlocked (const struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        return !!(signal->flags & MEDUSA_SIGNAL_FLAG_ENABLED);
}

__attribute__ ((visibility ("default"))) int medusa_signal_get_enabled (const struct medusa_signal *signal)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_get_enabled_unlocked(signal);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_enable (struct medusa_signal *signal)
{
        return medusa_signal_set_enabled(signal, 1);
}

__attribute__ ((visibility ("default"))) int medusa_signal_disable (struct medusa_signal *signal)
{
        return medusa_signal_set_enabled(signal, 0);
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_context_unlocked (struct medusa_signal *signal, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        signal->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_context (struct medusa_signal *signal, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_set_context_unlocked(signal, context);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_signal_get_context_unlocked (struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return signal->context;
}

__attribute__ ((visibility ("default"))) void * medusa_signal_get_context (struct medusa_signal *signal)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_get_context_unlocked(signal);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_userdata_unlocked (struct medusa_signal *signal, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        signal->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_userdata (struct medusa_signal *signal, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_set_userdata_unlocked(signal, userdata);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_signal_get_userdata_unlocked (struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return signal->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_signal_get_userdata (struct medusa_signal *signal)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_get_userdata_unlocked(signal);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_userdata_ptr_unlocked (struct medusa_signal *signal, void *userdata)
{
        return medusa_signal_set_userdata_unlocked(signal, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_userdata_ptr (struct medusa_signal *signal, void *userdata)
{
        return medusa_signal_set_userdata(signal, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_signal_get_userdata_ptr_unlocked (struct medusa_signal *signal)
{
        return medusa_signal_get_userdata_unlocked(signal);
}

__attribute__ ((visibility ("default"))) void * medusa_signal_get_userdata_ptr (struct medusa_signal *signal)
{
        return medusa_signal_get_userdata(signal);
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_userdata_int_unlocked (struct medusa_signal *signal, int userdata)
{
        return medusa_signal_set_userdata_unlocked(signal, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_userdata_int (struct medusa_signal *signal, int userdata)
{
        return medusa_signal_set_userdata(signal, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_signal_get_userdata_int_unlocked (struct medusa_signal *signal)
{
        return (int) (intptr_t) medusa_signal_get_userdata_unlocked(signal);
}

__attribute__ ((visibility ("default"))) int medusa_signal_get_userdata_int (struct medusa_signal *signal)
{
        return (int) (intptr_t) medusa_signal_get_userdata(signal);
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_userdata_uint_unlocked (struct medusa_signal *signal, unsigned int userdata)
{
        return medusa_signal_set_userdata_unlocked(signal, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_signal_set_userdata_uint (struct medusa_signal *signal, unsigned int userdata)
{
        return medusa_signal_set_userdata(signal, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_signal_get_userdata_uint_unlocked (struct medusa_signal *signal)
{
        return (unsigned int) (intptr_t) medusa_signal_get_userdata_unlocked(signal);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_signal_get_userdata_uint (struct medusa_signal *signal)
{
        return (unsigned int) (uintptr_t) medusa_signal_get_userdata(signal);
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_signal_get_monitor_unlocked (const struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return signal->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_signal_get_monitor (const struct medusa_signal *signal)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_get_monitor_unlocked(signal);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_onevent_unlocked (struct medusa_signal *signal, unsigned int events, void *param)
{
        int rc;
        struct medusa_monitor *monitor;
        rc = 0;
        monitor = signal->subject.monitor;
        if (events & MEDUSA_SIGNAL_EVENT_FIRED) {
                if (medusa_signal_get_singleshot_unlocked(signal)) {
                        rc = medusa_signal_set_enabled_unlocked(signal, 0);
                        if (rc < 0) {
                                return rc;
                        }
                }
        }
        if (signal->onevent != NULL) {
                if ((medusa_subject_is_active(&signal->subject)) ||
                    (events & MEDUSA_SIGNAL_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        rc = signal->onevent(signal, events, signal->context, param);
                        if (rc < 0) {
                                medusa_errorf("signal->onevent failed, rc: %d", rc);
                        }
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_SIGNAL_EVENT_FIRED) {
                if (signal->flags & MEDUSA_SIGNAL_FLAG_AUTO_DESTROY) {
                        medusa_signal_destroy_unlocked(signal);
                }
        }
        if (events & MEDUSA_SIGNAL_EVENT_DESTROY) {
#if defined(MEDUSA_SIGNAL_USE_POOL) && (MEDUSA_SIGNAL_USE_POOL == 1)
                medusa_pool_free(signal);
#else
                free(signal);
#endif
        }
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_onevent (struct medusa_signal *signal, unsigned int events, void *param)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return -EINVAL;
        }
        medusa_monitor_lock(signal->subject.monitor);
        rc = medusa_signal_onevent_unlocked(signal, events, param);
        medusa_monitor_unlock(signal->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_signal_is_valid_unlocked (const struct medusa_signal *signal)
{
        if (MEDUSA_IS_ERR_OR_NULL(signal)) {
                return 0;
        }
        if (signal->number < 0) {
                return 0;
        }
        if ((signal->flags & MEDUSA_SIGNAL_FLAG_ENABLED) == 0) {
                return 0;
        }
        return 1;
}

__attribute__ ((visibility ("default"))) const char * medusa_signal_event_string (unsigned int event)
{
        if (event == MEDUSA_SIGNAL_EVENT_FIRED)        return "MEDUSA_SIGNAL_EVENT_FIRED";
        if (event == MEDUSA_SIGNAL_EVENT_DESTROY)      return "MEDUSA_SIGNAL_EVENT_DESTROY";
        return "MEDUSA_SIGNAL_EVENT_UNKNOWN";
}

__attribute__ ((constructor)) static void signal_constructor (void)
{
#if defined(MEDUSA_SIGNAL_USE_POOL) && (MEDUSA_SIGNAL_USE_POOL == 1)
        g_pool = medusa_pool_create("medusa-signal", sizeof(struct medusa_signal), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void signal_destructor (void)
{
#if defined(MEDUSA_SIGNAL_USE_POOL) && (MEDUSA_SIGNAL_USE_POOL == 1)
        if (g_pool != NULL) {
                medusa_pool_destroy(g_pool);
        }
#endif
}
