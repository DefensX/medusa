
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MEDUSA_DEBUG_NAME       "dnsresolver"

#include "debug.h"
#include "error.h"
#include "pool.h"
#include "queue.h"
#include "subject-struct.h"
#include "clock.h"
#include "timer.h"
#include "timer-private.h"
#include "dnsrequest.h"
#include "dnsrequest-private.h"
#include "dnsresolver.h"
#include "dnsresolver-private.h"
#include "dnsresolver-struct.h"
#include "monitor-private.h"

#if !defined(MIN)
#define MIN(a, b)       (((a) < (b)) ? (a) : (b))
#endif

#define MEDUSA_DNSRESOLVER_USE_POOL             1

#if defined(MEDUSA_DNSRESOLVER_USE_POOL) && (MEDUSA_DNSRESOLVER_USE_POOL == 1)
static struct medusa_pool *g_pool_dnsresolver;
static struct medusa_pool *g_pool_dnsresolver_lookup;
#endif

static void dnsresolver_entry_destroy (struct medusa_dnsresolver_entry *entry)
{
        if (entry == NULL) {
                return;
        }
        if (entry->name != NULL) {
                free(entry->name);
        }
        if (entry->answers != NULL) {
                medusa_dnsrequest_reply_answers_destroy(entry->answers);
        }
        free(entry);
}

static inline unsigned int dnsresolver_get_state (const struct medusa_dnsresolver *dnsresolver)
{
        return dnsresolver->state;
}

static inline int dnsresolver_set_state (struct medusa_dnsresolver *dnsresolver, unsigned int state, unsigned int error)
{
        int rc;
        unsigned int pstate;
        struct medusa_dnsresolver_event_state_changed medusa_dnsresolver_event_state_changed;

        pstate = dnsresolver->state;
        dnsresolver->error = error;
        dnsresolver->state = state;

        medusa_dnsresolver_event_state_changed.pstate = pstate;
        medusa_dnsresolver_event_state_changed.state  = dnsresolver->state;
        medusa_dnsresolver_event_state_changed.error  = dnsresolver->error;
        rc = medusa_dnsresolver_onevent_unlocked(dnsresolver, MEDUSA_DNSRESOLVER_EVENT_STATE_CHANGED, &medusa_dnsresolver_event_state_changed);
        if (rc < 0) {
                return rc;
        }

        if (state == MEDUSA_DNSRESOLVER_STATE_STARTED) {
                rc = medusa_dnsresolver_onevent_unlocked(dnsresolver, MEDUSA_DNSRESOLVER_EVENT_STARTED, NULL);
                if (rc < 0) {
                        return rc;
                }
        } else if (state == MEDUSA_DNSRESOLVER_STATE_STOPPED) {
                struct medusa_dnsresolver_lookup *dnsresolver_lookup;
                struct medusa_dnsresolver_lookup *ndnsresolver_lookup;
                rc = medusa_dnsresolver_onevent_unlocked(dnsresolver, MEDUSA_DNSRESOLVER_EVENT_STOPPED, NULL);
                if (rc < 0) {
                        return rc;
                }
                TAILQ_FOREACH_SAFE(dnsresolver_lookup, &dnsresolver->lookups, tailq, ndnsresolver_lookup) {
                        medusa_dnsresolver_lookup_set_enabled_unlocked(dnsresolver_lookup, 0);
                }
        } else if (state == MEDUSA_DNSRESOLVER_STATE_ERROR) {
                struct medusa_dnsresolver_event_error medusa_dnsresolver_event_error;
                medusa_dnsresolver_event_error.state = pstate;
                medusa_dnsresolver_event_error.error = -EIO;
                rc = medusa_dnsresolver_onevent_unlocked(dnsresolver, MEDUSA_DNSRESOLVER_EVENT_ERROR, &medusa_dnsresolver_event_error);
                if (rc < 0) {
                        return rc;
                }
        }

        return 0;
}

static int dnsresolver_init_with_options_unlocked (struct medusa_dnsresolver *dnsresolver, const struct medusa_dnsresolver_init_options *options)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return -EINVAL;
        }
        memset(dnsresolver, 0, sizeof(struct medusa_dnsresolver));
        TAILQ_INIT(&dnsresolver->lookups);
        TAILQ_INIT(&dnsresolver->entries);
        medusa_subject_set_type(&dnsresolver->subject, MEDUSA_SUBJECT_TYPE_DNSRESOLVER);
        dnsresolver->subject.monitor = NULL;
        dnsresolver_set_state(dnsresolver, MEDUSA_DNSRESOLVER_EVENT_STOPPED, 0);
        dnsresolver->onevent = options->onevent;
        dnsresolver->context = options->context;
        rc = medusa_monitor_add_unlocked(options->monitor, &dnsresolver->subject);
        if (rc < 0) {
                return rc;
        }
        rc = medusa_dnsresolver_set_nameserver_unlocked(dnsresolver, options->nameserver);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_set_port_unlocked(dnsresolver, options->port);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_set_family_unlocked(dnsresolver, options->family);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_set_retry_count_unlocked(dnsresolver, options->retry_count);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_set_retry_interval_unlocked(dnsresolver, options->retry_interval);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_set_resolve_timeout_unlocked(dnsresolver, options->resolve_timeout);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_set_min_ttl_unlocked(dnsresolver, options->min_ttl);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_set_enabled_unlocked(dnsresolver, options->enabled);
        if (rc != 0) {
                return rc;
        }
        return 0;
}

static void dnsresolver_uninit_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return;
        }
        if (dnsresolver->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&dnsresolver->subject);
        } else {
                medusa_dnsresolver_onevent_unlocked(dnsresolver, MEDUSA_DNSRESOLVER_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_init_options_default (struct medusa_dnsresolver_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_dnsresolver_init_options));
        options->port           = 53;
        options->family         = MEDUSA_DNSRESOLVER_FAMILY_ANY;
        options->retry_count    = 3;
        options->retry_interval = 1.00;
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsresolver * medusa_dnsresolver_create_unlocked (struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_dnsresolver_init_options options;
        rc = medusa_dnsresolver_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor = monitor;
        options.onevent = onevent;
        options.context = context;
        return medusa_dnsresolver_create_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_dnsresolver * medusa_dnsresolver_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_dnsresolver *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_dnsresolver_create_unlocked(monitor, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsresolver * medusa_dnsresolver_create_with_options_unlocked (const struct medusa_dnsresolver_init_options *options)
{
        int rc;
        struct medusa_dnsresolver *dnsresolver;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_DNSRESOLVER_USE_POOL) && (MEDUSA_DNSRESOLVER_USE_POOL == 1)
        dnsresolver = medusa_pool_malloc(g_pool_dnsresolver);
#else
        dnsresolver = malloc(sizeof(struct medusa_dnsresolver));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(dnsresolver, 0, sizeof(struct medusa_dnsresolver));
        rc = dnsresolver_init_with_options_unlocked(dnsresolver, options);
        if (rc < 0) {
                medusa_dnsresolver_destroy_unlocked(dnsresolver);
                return MEDUSA_ERR_PTR(rc);
        }
        return dnsresolver;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsresolver * medusa_dnsresolver_create_with_options (const struct medusa_dnsresolver_init_options *options)
{
        struct medusa_dnsresolver *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_dnsresolver_create_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_dnsresolver_destroy_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return;
        }
        dnsresolver_uninit_unlocked(dnsresolver);
}

__attribute__ ((visibility ("default"))) void medusa_dnsresolver_destroy (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        medusa_dnsresolver_destroy_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_state_unlocked (const struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        return dnsresolver_get_state(dnsresolver);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_state (const struct medusa_dnsresolver *dnsresolver)
{
        unsigned int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_state_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_nameserver_unlocked (struct medusa_dnsresolver *dnsresolver, const char *nameserver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        if (dnsresolver->nameserver != NULL) {
                free(dnsresolver->nameserver);
        }
        if (nameserver != NULL) {
                dnsresolver->nameserver = strdup(nameserver);
                if (dnsresolver->nameserver == NULL) {
                        return -ENOMEM;
                }
        }
        return medusa_monitor_mod_unlocked(&dnsresolver->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_nameserver (struct medusa_dnsresolver *dnsresolver, const char *nameserver)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_nameserver_unlocked(dnsresolver, nameserver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_get_nameserver_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver->nameserver;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_get_nameserver (struct medusa_dnsresolver *dnsresolver)
{
        const char *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_nameserver_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_port_unlocked (struct medusa_dnsresolver *dnsresolver, int port)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        dnsresolver->port = port;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_port (struct medusa_dnsresolver *dnsresolver, int port)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_port_unlocked(dnsresolver, port);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_port_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        return dnsresolver->port;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_port (struct medusa_dnsresolver *dnsresolver)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_port_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_family_unlocked (struct medusa_dnsresolver *dnsresolver, unsigned int family)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        dnsresolver->family = family;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_family (struct medusa_dnsresolver *dnsresolver, unsigned int family)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_family_unlocked(dnsresolver, family);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_family_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        return dnsresolver->family;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_family (struct medusa_dnsresolver *dnsresolver)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_family_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_retry_count_unlocked (struct medusa_dnsresolver *dnsresolver, int retry_count)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        dnsresolver->retry_count = retry_count;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_retry_count (struct medusa_dnsresolver *dnsresolver, int retry_count)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_retry_count(dnsresolver, retry_count);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_retry_count_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        return dnsresolver->retry_count;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_retry_count (struct medusa_dnsresolver *dnsresolver)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_retry_count_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_retry_interval_unlocked (struct medusa_dnsresolver *dnsresolver, double retry_interval)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        dnsresolver->retry_interval = retry_interval;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_retry_interval (struct medusa_dnsresolver *dnsresolver, double retry_interval)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_retry_interval_unlocked(dnsresolver, retry_interval);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_dnsresolver_get_retry_interval_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        return dnsresolver->retry_interval;
}

__attribute__ ((visibility ("default"))) double medusa_dnsresolver_get_retry_interval (struct medusa_dnsresolver *dnsresolver)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_retry_interval_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_resolve_timeout_unlocked (struct medusa_dnsresolver *dnsresolver, double resolve_timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        dnsresolver->resolve_timeout = resolve_timeout;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_resolve_timeout (struct medusa_dnsresolver *dnsresolver, double resolve_timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_resolve_timeout(dnsresolver, resolve_timeout);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_dnsresolver_get_resolve_timeout_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        return dnsresolver->resolve_timeout;
}

__attribute__ ((visibility ("default"))) double medusa_dnsresolver_get_resolve_timeout (struct medusa_dnsresolver *dnsresolver)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_resolve_timeout(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_min_ttl_unlocked (struct medusa_dnsresolver *dnsresolver, int min_ttl)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        dnsresolver->min_ttl = min_ttl;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_min_ttl (struct medusa_dnsresolver *dnsresolver, int min_ttl)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_min_ttl(dnsresolver, min_ttl);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_min_ttl_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        return dnsresolver->min_ttl;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_min_ttl (struct medusa_dnsresolver *dnsresolver)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_min_ttl(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_context_unlocked (struct medusa_dnsresolver *dnsresolver, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        dnsresolver->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_context (struct medusa_dnsresolver *dnsresolver, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_context_unlocked(dnsresolver, context);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_get_context_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver->context;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_get_context (struct medusa_dnsresolver *dnsresolver)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_context_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_userdata_unlocked (struct medusa_dnsresolver *dnsresolver, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        dnsresolver->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_userdata (struct medusa_dnsresolver *dnsresolver, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_userdata_unlocked(dnsresolver, userdata);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_get_userdata_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_get_userdata (struct medusa_dnsresolver *dnsresolver)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_userdata_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_userdata_ptr_unlocked (struct medusa_dnsresolver *dnsresolver, void *userdata)
{
        return medusa_dnsresolver_set_userdata_unlocked(dnsresolver, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_userdata_ptr (struct medusa_dnsresolver *dnsresolver, void *userdata)
{
        return medusa_dnsresolver_set_userdata(dnsresolver, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_get_userdata_ptr_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        return medusa_dnsresolver_get_userdata_unlocked(dnsresolver);
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_get_userdata_ptr (struct medusa_dnsresolver *dnsresolver)
{
        return medusa_dnsresolver_get_userdata(dnsresolver);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_userdata_int_unlocked (struct medusa_dnsresolver *dnsresolver, int userdata)
{
        return medusa_dnsresolver_set_userdata_unlocked(dnsresolver, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_userdata_int (struct medusa_dnsresolver *dnsresolver, int userdata)
{
        return medusa_dnsresolver_set_userdata(dnsresolver, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_userdata_int_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        return (int) (intptr_t) medusa_dnsresolver_get_userdata_unlocked(dnsresolver);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_userdata_int (struct medusa_dnsresolver *dnsresolver)
{
        return (int) (intptr_t) medusa_dnsresolver_get_userdata(dnsresolver);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_userdata_uint_unlocked (struct medusa_dnsresolver *dnsresolver, unsigned int userdata)
{
        return medusa_dnsresolver_set_userdata_unlocked(dnsresolver, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_userdata_uint (struct medusa_dnsresolver *dnsresolver, unsigned int userdata)
{
        return medusa_dnsresolver_set_userdata(dnsresolver, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_dnsresolver_get_userdata_uint_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        return (unsigned int) (intptr_t) medusa_dnsresolver_get_userdata_unlocked(dnsresolver);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_dnsresolver_get_userdata_uint (struct medusa_dnsresolver *dnsresolver)
{
        return (unsigned int) (uintptr_t) medusa_dnsresolver_get_userdata(dnsresolver);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_onevent_unlocked (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *param)
{
        int ret;
        struct medusa_monitor *monitor;
        ret = 0;
        monitor = dnsresolver->subject.monitor;
        if (dnsresolver->onevent != NULL) {
                if ((medusa_subject_is_active(&dnsresolver->subject)) ||
                    (events & MEDUSA_DNSRESOLVER_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        ret = dnsresolver->onevent(dnsresolver, events, dnsresolver->context, param);
                        if (ret < 0) {
                                medusa_errorf("dnsresolver->onevent failed, rc: %d", ret);
                        }
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_DNSRESOLVER_EVENT_DESTROY) {
                struct medusa_dnsresolver_entry *dnsresolver_entry;
                struct medusa_dnsresolver_entry *ndnsresolver_entry;
                struct medusa_dnsresolver_lookup *dnsresolver_lookup;
                struct medusa_dnsresolver_lookup *ndnsresolver_lookup;
                TAILQ_FOREACH_SAFE(dnsresolver_entry, &dnsresolver->entries, tailq, ndnsresolver_entry) {
                        TAILQ_REMOVE(&dnsresolver->entries, dnsresolver_entry, tailq);
                        dnsresolver_entry_destroy(dnsresolver_entry);
                }
                TAILQ_FOREACH_SAFE(dnsresolver_lookup, &dnsresolver->lookups, tailq, ndnsresolver_lookup) {
                        TAILQ_REMOVE(&dnsresolver->lookups, dnsresolver_lookup, tailq);
                        dnsresolver_lookup->dnsresolver = NULL;
                        medusa_dnsresolver_lookup_destroy_unlocked(dnsresolver_lookup);
                }
                if (dnsresolver->nameserver != NULL) {
                        free(dnsresolver->nameserver);
                        dnsresolver->nameserver = NULL;
                }
#if defined(MEDUSA_DNSRESOLVER_USE_POOL) && (MEDUSA_DNSRESOLVER_USE_POOL == 1)
                medusa_pool_free(dnsresolver);
#else
                free(dnsresolver);
#endif
        }
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_enabled_unlocked (struct medusa_dnsresolver *dnsresolver, int enabled)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        if (dnsresolver->enabled == !!enabled) {
                return 0;
        }
        dnsresolver->enabled = !!enabled;
        return dnsresolver_set_state(dnsresolver, (dnsresolver->enabled) ? MEDUSA_DNSRESOLVER_STATE_STARTED : MEDUSA_DNSRESOLVER_STATE_STOPPED, 0);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_set_enabled (struct medusa_dnsresolver *dnsresolver, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_set_enabled_unlocked(dnsresolver, enabled);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_enabled_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        return dnsresolver->enabled;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_get_enabled (struct medusa_dnsresolver *dnsresolver)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_enabled_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_start_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        return medusa_dnsresolver_set_enabled_unlocked(dnsresolver, 1);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_start (struct medusa_dnsresolver *dnsresolver)
{
        return medusa_dnsresolver_set_enabled(dnsresolver, 1);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_stop_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        return medusa_dnsresolver_set_enabled_unlocked(dnsresolver, 0);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_stop (struct medusa_dnsresolver *dnsresolver)
{
        return medusa_dnsresolver_set_enabled(dnsresolver, 0);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_onevent (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *param)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_onevent_unlocked(dnsresolver, events, param);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_dnsresolver_get_monitor_unlocked (struct medusa_dnsresolver *dnsresolver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_dnsresolver_get_monitor (struct medusa_dnsresolver *dnsresolver)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_get_monitor_unlocked(dnsresolver);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_event_string (unsigned int events)
{
        if (events == MEDUSA_DNSRESOLVER_EVENT_STARTED)         return "MEDUSA_DNSRESOLVER_EVENT_STARTED";
        if (events == MEDUSA_DNSRESOLVER_EVENT_STOPPED)         return "MEDUSA_DNSRESOLVER_EVENT_STOPPED";
        if (events == MEDUSA_DNSRESOLVER_EVENT_ERROR)           return "MEDUSA_DNSRESOLVER_EVENT_ERROR";
        if (events == MEDUSA_DNSRESOLVER_EVENT_STATE_CHANGED)   return "MEDUSA_DNSRESOLVER_EVENT_STATE_CHANGED";
        if (events == MEDUSA_DNSRESOLVER_EVENT_DESTROY)         return "MEDUSA_DNSRESOLVER_EVENT_DESTROY";
        return "MEDUSA_DNSRESOLVER_EVENT_UNKNOWN";
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_state_string (unsigned int state)
{
        if (state == MEDUSA_DNSRESOLVER_STATE_UNKNOWN)          return "MEDUSA_DNSRESOLVER_STATE_UNKNOWN";
        if (state == MEDUSA_DNSRESOLVER_STATE_STARTED)          return "MEDUSA_DNSRESOLVER_STATE_STARTED";
        if (state == MEDUSA_DNSRESOLVER_STATE_STOPPED)          return "MEDUSA_DNSRESOLVER_STATE_STOPPED";
        if (state == MEDUSA_DNSRESOLVER_STATE_ERROR)            return "MEDUSA_DNSRESOLVER_STATE_ERROR";
        return "MEDUSA_DNSRESOLVER_STATE_UNKNOWN";
}

static inline unsigned int dnsresolver_lookup_get_state (const struct medusa_dnsresolver_lookup *dnsresolver_lookup);
static inline int dnsresolver_lookup_set_state (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int state, unsigned int error);

static int dnsrequest_onevent (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param)
{
        const struct medusa_dnsrequest_reply *dnsrequest_reply;
        const struct medusa_dnsrequest_reply_header *dnsrequest_reply_header;
        const struct medusa_dnsrequest_reply_answers *dnsrequest_reply_answers;
        const struct medusa_dnsrequest_reply_answer *dnsrequest_reply_answer;

        int rc;
        struct medusa_dnsresolver_lookup *dnsresolver_lookup = context;
        struct medusa_monitor *monitor = medusa_dnsresolver_lookup_get_monitor(dnsresolver_lookup);

        (void) param;

        medusa_monitor_lock(monitor);

        if (events & MEDUSA_DNSREQUEST_EVENT_RECEIVED) {
                int ttl;
                dnsrequest_reply = medusa_dnsrequest_get_reply_unlocked(dnsrequest);
                if (dnsrequest_reply == NULL) {
                        goto error;
                }
                dnsrequest_reply_header = medusa_dnsrequest_reply_get_header(dnsrequest_reply);
                if (dnsrequest_reply_header == NULL) {
                        goto error;
                }
                dnsrequest_reply_answers = medusa_dnsrequest_reply_get_answers(dnsrequest_reply);
                if (dnsrequest_reply_answers == NULL) {
                        goto error;
                }
                ttl = -1;
                for (dnsrequest_reply_answer = medusa_dnsrequest_reply_answers_get_first(dnsrequest_reply_answers);
                     dnsrequest_reply_answer != NULL;
                     dnsrequest_reply_answer = medusa_dnsrequest_reply_answer_get_next(dnsrequest_reply_answer)) {
                        struct medusa_dnsresolver_lookup_event_entry medusa_dnsresolver_lookup_event_entry;
                        switch (medusa_dnsrequest_reply_answer_get_type(dnsrequest_reply_answer)) {
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_A:
                                        medusa_dnsresolver_lookup_event_entry.family   = MEDUSA_DNSRESOLVER_FAMILY_IPV4;
                                        medusa_dnsresolver_lookup_event_entry.addreess = medusa_dnsrequest_reply_answer_a_get_address(dnsrequest_reply_answer);
                                        medusa_dnsresolver_lookup_event_entry.ttl      = medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer);
                                        rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY, &medusa_dnsresolver_lookup_event_entry);
                                        if (rc < 0) {
                                                medusa_errorf("medusa_dnsresolver_lookup_onevent_unlocked failed, rc: %d", rc);
                                                goto bail;
                                        }
                                        ttl = (ttl < 0) ? medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer) : MIN(ttl, medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA:
                                        medusa_dnsresolver_lookup_event_entry.family   = MEDUSA_DNSRESOLVER_FAMILY_IPV6;
                                        medusa_dnsresolver_lookup_event_entry.addreess = medusa_dnsrequest_reply_answer_aaaa_get_address(dnsrequest_reply_answer);
                                        medusa_dnsresolver_lookup_event_entry.ttl      = medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer);
                                        rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY, &medusa_dnsresolver_lookup_event_entry);
                                        if (rc < 0) {
                                                medusa_errorf("medusa_dnsresolver_lookup_onevent_unlocked failed, rc: %d", rc);
                                                goto bail;
                                        }
                                        ttl = (ttl < 0) ? medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer) : MIN(ttl, medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer));
                                        break;
                        }
                }
                if (medusa_dnsresolver_get_min_ttl_unlocked(dnsresolver_lookup->dnsresolver) >= 0) {
                        ttl = MIN(ttl, medusa_dnsresolver_get_min_ttl_unlocked(dnsresolver_lookup->dnsresolver));
                }
                if (ttl > 0) {
                        struct timespec now;
                        struct medusa_dnsresolver_entry *entry;
                        medusa_clock_monotonic_raw(&now);
                        entry = malloc(sizeof(struct medusa_dnsresolver_entry));
                        if (entry == NULL) {
                                medusa_errorf("can not allocate memory");
                                goto bail;
                        }
                        memset(entry, 0, sizeof(struct medusa_dnsresolver_entry));
                        entry->then.tv_sec = now.tv_sec + ttl;
                        entry->then.tv_nsec = now.tv_nsec;
                        entry->name = strdup(medusa_dnsresolver_lookup_get_name_unlocked(dnsresolver_lookup));
                        if (entry->name == NULL) {
                                medusa_errorf("can not allocate memory");
                                dnsresolver_entry_destroy(entry);
                                goto bail;
                        }
                        entry->answers = medusa_dnsrequest_reply_answers_copy(dnsrequest_reply_answers);
                        if (MEDUSA_IS_ERR_OR_NULL(entry->answers)) {
                                medusa_errorf("medusa_dnsrequest_reply_answers_copy failed, rc: %d", MEDUSA_PTR_ERR(entry->answers));
                                dnsresolver_entry_destroy(entry);
                                goto bail;
                        }
                        TAILQ_INSERT_TAIL(&dnsresolver_lookup->dnsresolver->entries, entry, tailq);
                }
                rc = dnsresolver_lookup_set_state(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_STATE_FINISHED, 0);
                if (rc < 0) {
                        medusa_errorf("dnsresolver_lookup_set_state failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_DNSREQUEST_EVENT_RESOLVE_TIMEOUT) {
                goto error;
        }
        if (events & MEDUSA_DNSREQUEST_EVENT_CONNECT_TIMEOUT) {
                goto error;
        }
        if (events & MEDUSA_DNSREQUEST_EVENT_RECEIVE_TIMEOUT) {
                goto error;
        }
        if (events & MEDUSA_DNSREQUEST_EVENT_ERROR) {
                goto error;
        }
        if (events & MEDUSA_DNSREQUEST_EVENT_DESTROY) {
                if (dnsresolver_lookup != NULL) {
                        dnsresolver_lookup->dnsrequest = NULL;
                }
        }

        medusa_monitor_unlock(monitor);
        return 0;
error:  dnsresolver_lookup_set_state(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_STATE_ERROR, -EIO);
        medusa_monitor_unlock(monitor);
        return 0;
bail:   medusa_monitor_unlock(monitor);
        return -1;
}

static int retry_interval_timer_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        struct medusa_dnsresolver_lookup *dnsresolver_lookup = context;
        struct medusa_monitor *monitor = medusa_dnsresolver_lookup_get_monitor(dnsresolver_lookup);

        (void) timer;
        (void) param;

        medusa_monitor_lock(monitor);

        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                if (dnsresolver_lookup->retried_count < dnsresolver_lookup->retry_count) {
                        int rc;
                        struct medusa_dnsrequest_init_options dnsrequest_init_options;
                        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->dnsrequest)) {
                                rc = medusa_dnsrequest_init_options_default(&dnsrequest_init_options);
                                if (rc < 0) {
                                        goto error;
                                }
                                dnsrequest_init_options.monitor         = dnsresolver_lookup->subject.monitor;
                                dnsrequest_init_options.onevent         = dnsrequest_onevent;
                                dnsrequest_init_options.context         = dnsresolver_lookup;
                                dnsrequest_init_options.nameserver      = medusa_dnsresolver_lookup_get_nameserver_unlocked(dnsresolver_lookup);
                                dnsrequest_init_options.port            = medusa_dnsresolver_lookup_get_port_unlocked(dnsresolver_lookup);
                                dnsrequest_init_options.type            = MEDUSA_DNSREQUEST_RECORD_TYPE_A;
                                dnsrequest_init_options.name            = medusa_dnsresolver_lookup_get_name_unlocked(dnsresolver_lookup);
                                dnsrequest_init_options.id              = medusa_dnsresolver_lookup_get_id_unlocked(dnsresolver_lookup);
                                dnsrequest_init_options.resolve_timeout = -1;
                                dnsrequest_init_options.connect_timeout = -1;
                                dnsrequest_init_options.receive_timeout = -1;
                                dnsrequest_init_options.enabled         = 1;
                                dnsresolver_lookup->dnsrequest = medusa_dnsrequest_create_with_options_unlocked(&dnsrequest_init_options);
                                if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->dnsrequest)) {
                                        goto error;
                                }
                        } else {
                                rc = medusa_dnsrequest_lookup_unlocked(dnsresolver_lookup->dnsrequest);
                                if (rc < 0) {
                                        goto error;
                                }
                        }
                        dnsresolver_lookup->retried_count += 1;
                }
        }
        if (events & MEDUSA_TIMER_EVENT_DESTROY) {
                if (dnsresolver_lookup != NULL) {
                        dnsresolver_lookup->retry_interval_timer = NULL;
                }
        }

        medusa_monitor_unlock(monitor);
        return 0;
error:  dnsresolver_lookup_set_state(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_STATE_ERROR, -EIO);
        medusa_monitor_unlock(monitor);
        return 0;
}

static int resolve_timeout_timer_onevent (struct medusa_timer *timer, unsigned int events, void *context, void *param)
{
        struct medusa_dnsresolver_lookup *dnsresolver_lookup = context;
        struct medusa_monitor *monitor = medusa_dnsresolver_lookup_get_monitor(dnsresolver_lookup);

        (void) timer;
        (void) param;

        medusa_monitor_lock(monitor);

        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                int rc;
                rc = dnsresolver_lookup_set_state(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_STATE_TIMEDOUT, 0);
                if (rc < 0) {
                        medusa_errorf("dnsresolver_lookup_set_state failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TIMER_EVENT_DESTROY) {
                if (dnsresolver_lookup != NULL) {
                        dnsresolver_lookup->resolve_timeout_timer = NULL;
                }
        }

        medusa_monitor_unlock(monitor);
        return 0;
bail:   medusa_monitor_unlock(monitor);
        return -1;
}

static inline unsigned int dnsresolver_lookup_get_state (const struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return dnsresolver_lookup->state;
}

static inline int dnsresolver_lookup_set_state (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int state, unsigned int error)
{
        int rc;
        unsigned int pstate;
        struct medusa_dnsresolver_lookup_event_state_changed medusa_dnsresolver_lookup_event_state_changed;

        if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_STARTED) {
                struct timespec now;
                struct medusa_dnsresolver_entry *entry;
                struct medusa_dnsresolver_entry *nentry;
                struct medusa_dnsrequest_init_options dnsrequest_init_options;

                medusa_clock_monotonic_raw(&now);
                TAILQ_FOREACH_SAFE(entry, &dnsresolver_lookup->dnsresolver->entries, tailq, nentry) {
                        const struct medusa_dnsrequest_reply_answer *dnsrequest_reply_answer;
                        if (medusa_timespec_compare(&now, &entry->then, >=)) {
                                TAILQ_REMOVE(&dnsresolver_lookup->dnsresolver->entries, entry, tailq);
                                dnsresolver_entry_destroy(entry);
                                continue;
                        }
                        if (strcasecmp(entry->name, medusa_dnsresolver_lookup_get_name_unlocked(dnsresolver_lookup)) != 0) {
                                continue;
                        }
                        rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STARTED, NULL);
                        if (rc < 0) {
                                return rc;
                        }
                        for (dnsrequest_reply_answer = medusa_dnsrequest_reply_answers_get_first(entry->answers);
                                dnsrequest_reply_answer != NULL;
                                dnsrequest_reply_answer = medusa_dnsrequest_reply_answer_get_next(dnsrequest_reply_answer)) {
                                struct medusa_dnsresolver_lookup_event_entry medusa_dnsresolver_lookup_event_entry;
                                switch (medusa_dnsrequest_reply_answer_get_type(dnsrequest_reply_answer)) {
                                        case MEDUSA_DNSREQUEST_RECORD_TYPE_A:
                                                medusa_dnsresolver_lookup_event_entry.family   = MEDUSA_DNSRESOLVER_FAMILY_IPV4;
                                                medusa_dnsresolver_lookup_event_entry.addreess = medusa_dnsrequest_reply_answer_a_get_address(dnsrequest_reply_answer);
                                                medusa_dnsresolver_lookup_event_entry.ttl      = medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer);
                                                rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY, &medusa_dnsresolver_lookup_event_entry);
                                                if (rc < 0) {
                                                        return rc;
                                                }
                                                break;
                                        case MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA:
                                                medusa_dnsresolver_lookup_event_entry.family   = MEDUSA_DNSRESOLVER_FAMILY_IPV6;
                                                medusa_dnsresolver_lookup_event_entry.addreess = medusa_dnsrequest_reply_answer_aaaa_get_address(dnsrequest_reply_answer);
                                                medusa_dnsresolver_lookup_event_entry.ttl      = medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer);
                                                rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY, &medusa_dnsresolver_lookup_event_entry);
                                                if (rc < 0) {
                                                        return rc;
                                                }
                                                break;
                                }
                        }
                        rc = dnsresolver_lookup_set_state(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_STATE_FINISHED, 0);
                        if (rc < 0) {
                                return rc;
                        }
                        return 0;
                }

                rc = medusa_dnsrequest_init_options_default(&dnsrequest_init_options);
                if (rc < 0) {
                        return rc;
                }
                dnsrequest_init_options.monitor         = dnsresolver_lookup->subject.monitor;
                dnsrequest_init_options.onevent         = dnsrequest_onevent;
                dnsrequest_init_options.context         = dnsresolver_lookup;
                dnsrequest_init_options.nameserver      = medusa_dnsresolver_lookup_get_nameserver_unlocked(dnsresolver_lookup);
                dnsrequest_init_options.port            = medusa_dnsresolver_lookup_get_port_unlocked(dnsresolver_lookup);
                dnsrequest_init_options.type            = MEDUSA_DNSREQUEST_RECORD_TYPE_A;
                dnsrequest_init_options.name            = medusa_dnsresolver_lookup_get_name_unlocked(dnsresolver_lookup);
                dnsrequest_init_options.id              = medusa_dnsresolver_lookup_get_id_unlocked(dnsresolver_lookup);;
                dnsrequest_init_options.resolve_timeout = -1;
                dnsrequest_init_options.connect_timeout = -1;
                dnsrequest_init_options.receive_timeout = -1;
                dnsrequest_init_options.enabled         = 1;
                dnsresolver_lookup->dnsrequest = medusa_dnsrequest_create_with_options_unlocked(&dnsrequest_init_options);
                if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->dnsrequest)) {
                        return MEDUSA_PTR_ERR(dnsresolver_lookup->dnsrequest);
                }
                if (dnsresolver_lookup->retry_interval >= 0) {
                        struct medusa_timer_init_options timer_init_options;
                        rc = medusa_timer_init_options_default(&timer_init_options);
                        if (rc < 0) {
                                return rc;
                        }
                        timer_init_options.monitor      = dnsresolver_lookup->subject.monitor;
                        timer_init_options.onevent      = retry_interval_timer_onevent;
                        timer_init_options.context      = dnsresolver_lookup;
                        timer_init_options.initial      = 0;
                        timer_init_options.interval     = dnsresolver_lookup->retry_interval;
                        timer_init_options.resolution   = MEDUSA_TIMER_RESOLUTION_DEFAULT;
                        timer_init_options.singleshot   = 0;
                        timer_init_options.enabled      = 1;
                        dnsresolver_lookup->retry_interval_timer = medusa_timer_create_with_options_unlocked(&timer_init_options);
                        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->retry_interval_timer)) {
                                return MEDUSA_PTR_ERR(dnsresolver_lookup->retry_interval_timer);
                        }
                }
                if (dnsresolver_lookup->resolve_timeout >= 0) {
                        struct medusa_timer_init_options timer_init_options;
                        rc = medusa_timer_init_options_default(&timer_init_options);
                        if (rc < 0) {
                                return rc;
                        }
                        timer_init_options.monitor      = dnsresolver_lookup->subject.monitor;
                        timer_init_options.onevent      = resolve_timeout_timer_onevent;
                        timer_init_options.context      = dnsresolver_lookup;
                        timer_init_options.initial      = 0;
                        timer_init_options.interval     = dnsresolver_lookup->resolve_timeout;
                        timer_init_options.resolution   = MEDUSA_TIMER_RESOLUTION_DEFAULT;
                        timer_init_options.singleshot   = 0;
                        timer_init_options.enabled      = 1;
                        dnsresolver_lookup->resolve_timeout_timer = medusa_timer_create_with_options_unlocked(&timer_init_options);
                        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->resolve_timeout_timer)) {
                                return MEDUSA_PTR_ERR(dnsresolver_lookup->resolve_timeout_timer);
                        }
                }
                dnsresolver_lookup->retried_count = 0;
                rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STARTED, NULL);
                if (rc < 0) {
                        return rc;
                }
        } else if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_STOPPED) {
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->resolve_timeout_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->resolve_timeout_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->resolve_timeout_timer);
                        dnsresolver_lookup->resolve_timeout_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->retry_interval_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->retry_interval_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->retry_interval_timer);
                        dnsresolver_lookup->retry_interval_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->dnsrequest)) {
                        medusa_dnsrequest_set_context_unlocked(dnsresolver_lookup->dnsrequest, NULL);
                        medusa_dnsrequest_destroy_unlocked(dnsresolver_lookup->dnsrequest);
                        dnsresolver_lookup->dnsrequest = NULL;
                }
                rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STOPPED, NULL);
                if (rc < 0) {
                        return rc;
                }
        } else if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_FINISHED) {
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->resolve_timeout_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->resolve_timeout_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->resolve_timeout_timer);
                        dnsresolver_lookup->resolve_timeout_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->retry_interval_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->retry_interval_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->retry_interval_timer);
                        dnsresolver_lookup->retry_interval_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->dnsrequest)) {
                        medusa_dnsrequest_set_context_unlocked(dnsresolver_lookup->dnsrequest, NULL);
                        medusa_dnsrequest_destroy_unlocked(dnsresolver_lookup->dnsrequest);
                        dnsresolver_lookup->dnsrequest = NULL;
                }
                rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_FINISHED, NULL);
                if (rc < 0) {
                        return rc;
                }
        } else if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_TIMEDOUT) {
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->resolve_timeout_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->resolve_timeout_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->resolve_timeout_timer);
                        dnsresolver_lookup->resolve_timeout_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->retry_interval_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->retry_interval_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->retry_interval_timer);
                        dnsresolver_lookup->retry_interval_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->dnsrequest)) {
                        medusa_dnsrequest_set_context_unlocked(dnsresolver_lookup->dnsrequest, NULL);
                        medusa_dnsrequest_destroy_unlocked(dnsresolver_lookup->dnsrequest);
                        dnsresolver_lookup->dnsrequest = NULL;
                }
                rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_TIMEDOUT, NULL);
                if (rc < 0) {
                        return rc;
                }
        } else if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_ERROR) {
                struct medusa_dnsresolver_event_error medusa_dnsresolver_event_error;
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->resolve_timeout_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->resolve_timeout_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->resolve_timeout_timer);
                        dnsresolver_lookup->resolve_timeout_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->retry_interval_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->retry_interval_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->retry_interval_timer);
                        dnsresolver_lookup->retry_interval_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->dnsrequest)) {
                        medusa_dnsrequest_set_context_unlocked(dnsresolver_lookup->dnsrequest, NULL);
                        medusa_dnsrequest_destroy_unlocked(dnsresolver_lookup->dnsrequest);
                        dnsresolver_lookup->dnsrequest = NULL;
                }
                medusa_dnsresolver_event_error.state = dnsresolver_lookup->state;
                medusa_dnsresolver_event_error.error = error;
                rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ERROR, &medusa_dnsresolver_event_error);
                if (rc < 0) {
                        return rc;
                }
        }

        pstate = dnsresolver_lookup->state;
        dnsresolver_lookup->error = error;
        dnsresolver_lookup->state = state;

        medusa_dnsresolver_lookup_event_state_changed.pstate = pstate;
        medusa_dnsresolver_lookup_event_state_changed.state  = dnsresolver_lookup->state;
        medusa_dnsresolver_lookup_event_state_changed.error  = dnsresolver_lookup->error;
        rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STATE_CHANGED, &medusa_dnsresolver_lookup_event_state_changed);
        if (rc < 0) {
                return rc;
        }

        return 0;
}

static int dnsresolver_lookup_init_with_options_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, struct medusa_dnsresolver *dnsresolver, const struct medusa_dnsresolver_lookup_options *options)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return -EINVAL;
        }
        memset(dnsresolver_lookup, 0, sizeof(struct medusa_dnsresolver_lookup));
        medusa_subject_set_type(&dnsresolver_lookup->subject, MEDUSA_SUBJECT_TYPE_DNSRESOLVER_LOOKUP);
        dnsresolver_lookup->subject.monitor = NULL;
        dnsresolver_lookup_set_state(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STOPPED, 0);
        dnsresolver_lookup->onevent = options->onevent;
        dnsresolver_lookup->context = options->context;
        rc = medusa_monitor_add_unlocked(dnsresolver->subject.monitor, &dnsresolver_lookup->subject);
        if (rc < 0) {
                return rc;
        }
        dnsresolver_lookup->dnsresolver = dnsresolver;
        TAILQ_INSERT_TAIL(&dnsresolver->lookups, dnsresolver_lookup, tailq);
        rc = medusa_dnsresolver_lookup_set_nameserver_unlocked(dnsresolver_lookup, (options->nameserver == NULL) ? medusa_dnsresolver_get_nameserver_unlocked(dnsresolver) : options->nameserver);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_lookup_set_port_unlocked(dnsresolver_lookup, (options->port == 0) ? medusa_dnsresolver_get_port_unlocked(dnsresolver) : options->port);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_lookup_set_name_unlocked(dnsresolver_lookup, options->name);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_lookup_set_id_unlocked(dnsresolver_lookup, options->id > 0 ? options->id : (rand() & 0xffff));
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_lookup_set_family_unlocked(dnsresolver_lookup, (options->family == MEDUSA_DNSRESOLVER_FAMILY_ANY) ? (unsigned int) medusa_dnsresolver_get_family_unlocked(dnsresolver) : options->family);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_lookup_set_retry_count_unlocked(dnsresolver_lookup, (options->retry_count < 0) ? medusa_dnsresolver_get_retry_count_unlocked(dnsresolver) : options->retry_count);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_lookup_set_retry_interval_unlocked(dnsresolver_lookup, (options->retry_interval < 0) ? medusa_dnsresolver_get_retry_interval_unlocked(dnsresolver) : options->retry_interval);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_lookup_set_resolve_timeout_unlocked(dnsresolver_lookup, (options->resolve_timeout < 0) ? medusa_dnsresolver_get_resolve_timeout_unlocked(dnsresolver) : options->resolve_timeout);
        if (rc != 0) {
                return rc;
        }
        rc = medusa_dnsresolver_lookup_set_enabled_unlocked(dnsresolver_lookup, (options->enabled < 0) ? medusa_dnsresolver_get_enabled_unlocked(dnsresolver) : options->enabled);
        if (rc != 0) {
                return rc;
        }
        return 0;
}

static void dnsresolver_lookup_uninit_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return;
        }
        if (dnsresolver_lookup->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&dnsresolver_lookup->subject);
        } else {
                medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_options_default (struct medusa_dnsresolver_lookup_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_dnsresolver_lookup_options));
        options->port            = 0;
        options->id              = -1;
        options->family          = MEDUSA_DNSRESOLVER_FAMILY_ANY;
        options->retry_count     = -1;
        options->retry_interval  = -1;
        options->resolve_timeout = -1;
        options->enabled         = -1;
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsresolver_lookup * medusa_dnsresolver_lookup_unlocked (struct medusa_dnsresolver *dnsresolver, unsigned int family, const char *name, int (*onevent) (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_dnsresolver_lookup_options options;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        rc = medusa_dnsresolver_lookup_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.family          = family;
        options.name            = name;
        options.onevent         = onevent;
        options.context         = context;
        options.retry_count     = medusa_dnsresolver_get_retry_count_unlocked(dnsresolver);
        options.retry_interval  = medusa_dnsresolver_get_retry_interval_unlocked(dnsresolver);
        options.resolve_timeout = medusa_dnsresolver_get_resolve_timeout_unlocked(dnsresolver);
        options.enabled         = medusa_dnsresolver_get_enabled_unlocked(dnsresolver);
        return medusa_dnsresolver_lookup_with_options_unlocked(dnsresolver, &options);
}

__attribute__ ((visibility ("default"))) struct medusa_dnsresolver_lookup * medusa_dnsresolver_lookup (struct medusa_dnsresolver *dnsresolver, unsigned int family, const char *name, int (*onevent) (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_dnsresolver_lookup *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_lookup_unlocked(dnsresolver, family, name, onevent, context);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsresolver_lookup * medusa_dnsresolver_lookup_with_options_unlocked (struct medusa_dnsresolver *dnsresolver, const struct medusa_dnsresolver_lookup_options *options)
{
        int rc;
        struct medusa_dnsresolver_lookup *dnsresolver_lookup;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_DNSRESOLVER_USE_POOL) && (MEDUSA_DNSRESOLVER_USE_POOL == 1)
        dnsresolver_lookup = medusa_pool_malloc(g_pool_dnsresolver_lookup);
#else
        dnsresolver_lookup = malloc(sizeof(struct medusa_dnsresolver_lookup));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(dnsresolver_lookup, 0, sizeof(struct medusa_dnsresolver_lookup));
        rc = dnsresolver_lookup_init_with_options_unlocked(dnsresolver_lookup, dnsresolver, options);
        if (rc < 0) {
                medusa_dnsresolver_lookup_destroy_unlocked(dnsresolver_lookup);
                return MEDUSA_ERR_PTR(rc);
        }
        return dnsresolver_lookup;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsresolver_lookup * medusa_dnsresolver_lookup_with_options (struct medusa_dnsresolver *dnsresolver, const struct medusa_dnsresolver_lookup_options *options)
{
        struct medusa_dnsresolver_lookup *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver->subject.monitor);
        rc = medusa_dnsresolver_lookup_with_options_unlocked(dnsresolver, options);
        medusa_monitor_unlock(dnsresolver->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_dnsresolver_lookup_destroy_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return;
        }
        dnsresolver_lookup_uninit_unlocked(dnsresolver_lookup);
}

__attribute__ ((visibility ("default"))) void medusa_dnsresolver_lookup_destroy (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        medusa_dnsresolver_lookup_destroy_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_state_unlocked (const struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        return dnsresolver_lookup_get_state(dnsresolver_lookup);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_state (const struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_state_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_nameserver_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, const char *nameserver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        if (dnsresolver_lookup->nameserver != NULL) {
                free(dnsresolver_lookup->nameserver);
        }
        if (nameserver != NULL) {
                dnsresolver_lookup->nameserver = strdup(nameserver);
                if (dnsresolver_lookup->nameserver == NULL) {
                        return -ENOMEM;
                }
        }
        return medusa_monitor_mod_unlocked(&dnsresolver_lookup->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_nameserver (struct medusa_dnsresolver_lookup *dnsresolver_lookup, const char *nameserver)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_nameserver_unlocked(dnsresolver_lookup, nameserver);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_lookup_get_nameserver_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver_lookup->nameserver;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_lookup_get_nameserver (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        const char *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_nameserver_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_port_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int port)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        dnsresolver_lookup->port = port;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_port (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int port)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_port_unlocked(dnsresolver_lookup, port);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_port_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        return dnsresolver_lookup->port;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_port (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_port_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_family_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int family)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        dnsresolver_lookup->family = family;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_family (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int family)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_family_unlocked(dnsresolver_lookup, family);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_family_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        return dnsresolver_lookup->family;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_family (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_family_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_name_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, const char *name)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        if (dnsresolver_lookup->name != NULL) {
                free(dnsresolver_lookup->name);
        }
        if (name != NULL) {
                dnsresolver_lookup->name = strdup(name);
                if (dnsresolver_lookup->name == NULL) {
                        return -ENOMEM;
                }
        }
        return medusa_monitor_mod_unlocked(&dnsresolver_lookup->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_name (struct medusa_dnsresolver_lookup *dnsresolver_lookup, const char *name)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_name_unlocked(dnsresolver_lookup, name);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_lookup_get_name_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver_lookup->name;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_lookup_get_name (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        const char *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_name_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_id_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int id)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        if (id < 0x0000 || id > 0xffff) {
                return -EINVAL;
        }
        dnsresolver_lookup->id = id;
        return medusa_monitor_mod_unlocked(&dnsresolver_lookup->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_id (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int id)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_id_unlocked(dnsresolver_lookup, id);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_id_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        return dnsresolver_lookup->id;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_id (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_id_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_retry_count_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int retry_count)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        dnsresolver_lookup->retry_count = retry_count;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_retry_count (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int retry_count)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_retry_count(dnsresolver_lookup, retry_count);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_retry_count_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        return dnsresolver_lookup->retry_count;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_retry_count (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_retry_count_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_retry_interval_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, double retry_interval)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        dnsresolver_lookup->retry_interval = retry_interval;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_retry_interval (struct medusa_dnsresolver_lookup *dnsresolver_lookup, double retry_interval)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_retry_interval_unlocked(dnsresolver_lookup, retry_interval);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_dnsresolver_lookup_get_retry_interval_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        return dnsresolver_lookup->retry_interval;
}

__attribute__ ((visibility ("default"))) double medusa_dnsresolver_lookup_get_retry_interval (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_retry_interval_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_resolve_timeout_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, double resolve_timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        dnsresolver_lookup->resolve_timeout = resolve_timeout;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_resolve_timeout (struct medusa_dnsresolver_lookup *dnsresolver_lookup, double resolve_timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_resolve_timeout(dnsresolver_lookup, resolve_timeout);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_dnsresolver_lookup_get_resolve_timeout_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        return dnsresolver_lookup->resolve_timeout;
}

__attribute__ ((visibility ("default"))) double medusa_dnsresolver_lookup_get_resolve_timeout (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_resolve_timeout(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_context_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        dnsresolver_lookup->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_context (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_context_unlocked(dnsresolver_lookup, context);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_lookup_get_context_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver_lookup->context;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_lookup_get_context (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_context_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_userdata_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        dnsresolver_lookup->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_userdata (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_userdata_unlocked(dnsresolver_lookup, userdata);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_lookup_get_userdata_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver_lookup->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_lookup_get_userdata (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_userdata_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_userdata_ptr_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *userdata)
{
        return medusa_dnsresolver_lookup_set_userdata_unlocked(dnsresolver_lookup, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_userdata_ptr (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *userdata)
{
        return medusa_dnsresolver_lookup_set_userdata(dnsresolver_lookup, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_lookup_get_userdata_ptr_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return medusa_dnsresolver_lookup_get_userdata_unlocked(dnsresolver_lookup);
}

__attribute__ ((visibility ("default"))) void * medusa_dnsresolver_lookup_get_userdata_ptr (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return medusa_dnsresolver_lookup_get_userdata(dnsresolver_lookup);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_userdata_int_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int userdata)
{
        return medusa_dnsresolver_lookup_set_userdata_unlocked(dnsresolver_lookup, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_userdata_int (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int userdata)
{
        return medusa_dnsresolver_lookup_set_userdata(dnsresolver_lookup, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_userdata_int_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return (int) (intptr_t) medusa_dnsresolver_lookup_get_userdata_unlocked(dnsresolver_lookup);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_userdata_int (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return (int) (intptr_t) medusa_dnsresolver_lookup_get_userdata(dnsresolver_lookup);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_userdata_uint_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int userdata)
{
        return medusa_dnsresolver_lookup_set_userdata_unlocked(dnsresolver_lookup, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_userdata_uint (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int userdata)
{
        return medusa_dnsresolver_lookup_set_userdata(dnsresolver_lookup, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_dnsresolver_lookup_get_userdata_uint_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return (unsigned int) (intptr_t) medusa_dnsresolver_lookup_get_userdata_unlocked(dnsresolver_lookup);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_dnsresolver_lookup_get_userdata_uint (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return (unsigned int) (uintptr_t) medusa_dnsresolver_lookup_get_userdata(dnsresolver_lookup);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_onevent_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *param)
{
        int ret;
        struct medusa_monitor *monitor;
        ret = 0;
        monitor = dnsresolver_lookup->subject.monitor;
        if (dnsresolver_lookup->onevent != NULL) {
                if ((medusa_subject_is_active(&dnsresolver_lookup->subject)) ||
                    (events & MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        ret = dnsresolver_lookup->onevent(dnsresolver_lookup, events, dnsresolver_lookup->context, param);
                        if (ret < 0) {
                                medusa_errorf("dnsresolver_lookup->onevent failed, rc: %d", ret);
                        }
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY) {
                if (dnsresolver_lookup->nameserver != NULL) {
                        free(dnsresolver_lookup->nameserver);
                        dnsresolver_lookup->nameserver = NULL;
                }
                if (dnsresolver_lookup->name != NULL) {
                        free(dnsresolver_lookup->name);
                        dnsresolver_lookup->name = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->resolve_timeout_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->resolve_timeout_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->resolve_timeout_timer);
                        dnsresolver_lookup->resolve_timeout_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->retry_interval_timer)) {
                        medusa_timer_set_context_unlocked(dnsresolver_lookup->retry_interval_timer, NULL);
                        medusa_timer_destroy_unlocked(dnsresolver_lookup->retry_interval_timer);
                        dnsresolver_lookup->retry_interval_timer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup->dnsrequest)) {
                        medusa_dnsrequest_set_context_unlocked(dnsresolver_lookup->dnsrequest, NULL);
                        medusa_dnsrequest_destroy_unlocked(dnsresolver_lookup->dnsrequest);
                        dnsresolver_lookup->dnsrequest = NULL;
                }
                if (dnsresolver_lookup->dnsresolver != NULL) {
                        TAILQ_REMOVE(&dnsresolver_lookup->dnsresolver->lookups, dnsresolver_lookup, tailq);
                        dnsresolver_lookup->dnsresolver = NULL;
                }
#if defined(MEDUSA_DNSRESOLVER_USE_POOL) && (MEDUSA_DNSRESOLVER_USE_POOL == 1)
                medusa_pool_free(dnsresolver_lookup);
#else
                free(dnsresolver_lookup);
#endif
        }
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_enabled_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int enabled)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        if (enabled) {
                if (medusa_dnsresolver_get_state_unlocked(dnsresolver_lookup->dnsresolver) != MEDUSA_DNSRESOLVER_STATE_STARTED) {
                        return -EINVAL;
                }
        }
        dnsresolver_lookup->enabled = !!enabled;
        return dnsresolver_lookup_set_state(dnsresolver_lookup, (dnsresolver_lookup->enabled) ? MEDUSA_DNSRESOLVER_LOOKUP_STATE_STARTED : MEDUSA_DNSRESOLVER_LOOKUP_STATE_STOPPED, 0);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_set_enabled (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_set_enabled_unlocked(dnsresolver_lookup, enabled);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_enabled_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        return dnsresolver_lookup->enabled;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_get_enabled (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_enabled_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_start_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return medusa_dnsresolver_lookup_set_enabled_unlocked(dnsresolver_lookup, 1);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_start (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return medusa_dnsresolver_lookup_set_enabled(dnsresolver_lookup, 1);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_stop_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return medusa_dnsresolver_lookup_set_enabled_unlocked(dnsresolver_lookup, 0);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_stop (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        return medusa_dnsresolver_lookup_set_enabled(dnsresolver_lookup, 0);
}

__attribute__ ((visibility ("default"))) int medusa_dnsresolver_lookup_onevent (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *param)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_onevent_unlocked(dnsresolver_lookup, events, param);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_dnsresolver_lookup_get_monitor_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsresolver_lookup->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_dnsresolver_lookup_get_monitor (struct medusa_dnsresolver_lookup *dnsresolver_lookup)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsresolver_lookup)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsresolver_lookup->subject.monitor);
        rc = medusa_dnsresolver_lookup_get_monitor_unlocked(dnsresolver_lookup);
        medusa_monitor_unlock(dnsresolver_lookup->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_lookup_event_string (unsigned int events)
{
        if (events == MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STARTED)          return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STARTED";
        if (events == MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STOPPED)          return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STOPPED";
        if (events == MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY)            return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY";
        if (events == MEDUSA_DNSRESOLVER_LOOKUP_EVENT_FINISHED)         return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_FINISHED";
        if (events == MEDUSA_DNSRESOLVER_LOOKUP_EVENT_TIMEDOUT)         return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_TIMEDOUT";
        if (events == MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ERROR)            return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ERROR";
        if (events == MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STATE_CHANGED)    return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STATE_CHANGED";
        if (events == MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY)          return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY";
        return "MEDUSA_DNSRESOLVER_LOOKUP_EVENT_UNKNOWN";
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsresolver_lookup_state_string (unsigned int state)
{
        if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_UNKNOWN)          return "MEDUSA_DNSRESOLVER_LOOKUP_STATE_UNKNOWN";
        if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_STARTED)          return "MEDUSA_DNSRESOLVER_LOOKUP_STATE_STARTED";
        if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_STOPPED)          return "MEDUSA_DNSRESOLVER_LOOKUP_STATE_STOPPED";
        if (state == MEDUSA_DNSRESOLVER_LOOKUP_STATE_ERROR)            return "MEDUSA_DNSRESOLVER_LOOKUP_STATE_ERROR";
        return "MEDUSA_DNSRESOLVER_LOOKUP_STATE_UNKNOWN";
}

__attribute__ ((constructor)) static void dnsresolver_constructor (void)
{
#if defined(MEDUSA_DNSRESOLVER_USE_POOL) && (MEDUSA_DNSRESOLVER_USE_POOL == 1)
        g_pool_dnsresolver = medusa_pool_create("medusa-dnsresolver", sizeof(struct medusa_dnsresolver), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
        g_pool_dnsresolver_lookup = medusa_pool_create("medusa-dnsresolver-lookup", sizeof(struct medusa_dnsresolver_lookup), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void dnsresolver_destructor (void)
{
#if defined(MEDUSA_DNSRESOLVER_USE_POOL) && (MEDUSA_DNSRESOLVER_USE_POOL == 1)
        if (g_pool_dnsresolver != NULL) {
                medusa_pool_destroy(g_pool_dnsresolver);
        }
        if (g_pool_dnsresolver_lookup != NULL) {
                medusa_pool_destroy(g_pool_dnsresolver_lookup);
        }
#endif
}
