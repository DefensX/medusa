
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MEDUSA_DEBUG_NAME       "timer"

#include "clock.h"
#include "debug.h"
#include "error.h"
#include "pool.h"
#include "queue.h"
#include "monitor.h"
#include "monitor-private.h"
#include "subject-struct.h"
#include "timer-struct.h"
#include "timer-private.h"

#include "timer.h"

#define MEDUSA_TIMER_USE_POOL   1
#if defined(MEDUSA_TIMER_USE_POOL) && (MEDUSA_TIMER_USE_POOL == 1)
static struct medusa_pool *g_pool;
#endif

enum {
        MEDUSA_TIMER_FLAG_ENABLED       = (1 <<  0),
        MEDUSA_TIMER_FLAG_SINGLE_SHOT   = (1 <<  1),
        MEDUSA_TIMER_FLAG_AUTO_DESTROY  = (1 <<  2),
        MEDUSA_TIMER_FLAG_TICK          = (1 <<  3),
        MEDUSA_TIMER_FLAG_INCREMENTAL   = (1 <<  4),
        MEDUSA_TIMER_FLAG_NANOSECONDS   = (1 <<  5),
        MEDUSA_TIMER_FLAG_MICROSECONDS  = (1 <<  6),
        MEDUSA_TIMER_FLAG_MILLISECONDS  = (1 <<  7),
        MEDUSA_TIMER_FLAG_SECONDS       = (1 <<  8),
        MEDUSA_TIMER_FLAG_FIRED         = (1 <<  9),
        MEDUSA_TIMER_FLAG_INITIAL       = (1 << 10),
        MEDUSA_TIMER_FLAG_INTERVAL      = (1 << 11),
#define MEDUSA_TIMER_FLAG_ENABLED       MEDUSA_TIMER_FLAG_ENABLED
#define MEDUSA_TIMER_FLAG_SINGLE_SHOT   MEDUSA_TIMER_FLAG_SINGLE_SHOT
#define MEDUSA_TIMER_FLAG_AUTO_DESTROY  MEDUSA_TIMER_FLAG_AUTO_DESTROY
#define MEDUSA_TIMER_FLAG_TICK          MEDUSA_TIMER_FLAG_TICK
#define MEDUSA_TIMER_FLAG_INCREMENTAL   MEDUSA_TIMER_FLAG_INCREMENTAL
#define MEDUSA_TIMER_FLAG_NANOSECONDS   MEDUSA_TIMER_FLAG_NANOSECONDS
#define MEDUSA_TIMER_FLAG_MICROSECONDS  MEDUSA_TIMER_FLAG_MICROSECONDS
#define MEDUSA_TIMER_FLAG_MILLISECONDS  MEDUSA_TIMER_FLAG_MILLISECONDS
#define MEDUSA_TIMER_FLAG_SECONDS       MEDUSA_TIMER_FLAG_SECONDS
#define MEDUSA_TIMER_FLAG_FIRED         MEDUSA_TIMER_FLAG_FIRED
#define MEDUSA_TIMER_FLAG_INITIAL       MEDUSA_TIMER_FLAG_INITIAL
#define MEDUSA_TIMER_FLAG_INTERVAL      MEDUSA_TIMER_FLAG_INTERVAL
};

static int timer_set_initial_timespec (struct medusa_timer *timer, const struct timespec *initial)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(initial)) {
                return -EINVAL;
        }
        timer->initial.tv_sec = initial->tv_sec;
        timer->initial.tv_nsec = initial->tv_nsec;
        timer->flags |= MEDUSA_TIMER_FLAG_INITIAL;
        return 0;
}

static int timer_set_initial (struct medusa_timer *timer, double initial)
{
        struct timespec timespec;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (initial < 0) {
                return -EINVAL;
        }
        timespec.tv_sec = (long long) initial;
        timespec.tv_nsec = (long long) ((initial - timespec.tv_sec) * 1e9);
        return timer_set_initial_timespec(timer, &timespec);
}

static int timer_set_interval_timespec (struct medusa_timer *timer, const struct timespec *interval)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(interval)) {
                return -EINVAL;
        }
        timer->interval.tv_sec = interval->tv_sec;
        timer->interval.tv_nsec = interval->tv_nsec;
        timer->flags |= MEDUSA_TIMER_FLAG_INTERVAL;
        return 0;
}

static int timer_set_interval (struct medusa_timer *timer, double interval)
{
        struct timespec timespec;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (interval < 0) {
                return -EINVAL;
        }
        timespec.tv_sec = (long long) interval;
        timespec.tv_nsec = (long long) ((interval - timespec.tv_sec) * 1e9);
        return timer_set_interval_timespec(timer, &timespec);
}

static int timer_set_singleshot (struct medusa_timer *timer, int singleshot)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (singleshot) {
                timer->flags |= MEDUSA_TIMER_FLAG_SINGLE_SHOT;
        } else {
                timer->flags &= ~MEDUSA_TIMER_FLAG_SINGLE_SHOT;
        }
        return 0;
}

static int timer_set_resolution (struct medusa_timer *timer, unsigned int resolution)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        timer->flags &= ~MEDUSA_TIMER_FLAG_NANOSECONDS;
        timer->flags &= ~MEDUSA_TIMER_FLAG_MICROSECONDS;
        timer->flags &= ~MEDUSA_TIMER_FLAG_MILLISECONDS;
        timer->flags &= ~MEDUSA_TIMER_FLAG_SECONDS;
        if (resolution == MEDUSA_TIMER_RESOLUTION_NANOSECONDS) {
                timer->flags |= MEDUSA_TIMER_FLAG_NANOSECONDS;
        } else if (resolution == MEDUSA_TIMER_RESOLUTION_MICROSECONDS) {
                timer->flags |= MEDUSA_TIMER_FLAG_MICROSECONDS;
        } else if (resolution == MEDUSA_TIMER_RESOLUTION_MILLISECONDS) {
                timer->flags |= MEDUSA_TIMER_FLAG_MILLISECONDS;
        } else if (resolution == MEDUSA_TIMER_RESOLUTION_SECONDS) {
                timer->flags |= MEDUSA_TIMER_FLAG_SECONDS;
        } else {
                timer->flags |= MEDUSA_TIMER_FLAG_MILLISECONDS;
        }
        return 0;
}

static int timer_set_accuracy (struct medusa_timer *timer, unsigned int accuracy)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        timer->flags &= ~MEDUSA_TIMER_FLAG_TICK;
        timer->flags &= ~MEDUSA_TIMER_FLAG_INCREMENTAL;
        if (accuracy == MEDUSA_TIMER_ACCURACY_TICK) {
                timer->flags |= MEDUSA_TIMER_FLAG_TICK;
        } else if (accuracy == MEDUSA_TIMER_ACCURACY_INCREMENTAL) {
                timer->flags |= MEDUSA_TIMER_FLAG_INCREMENTAL;
        } else {
                timer->flags |= MEDUSA_TIMER_FLAG_INCREMENTAL;
        }
        return 0;
}

static int timer_set_enabled (struct medusa_timer *timer, int enabled)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (enabled) {
                timer->flags |= MEDUSA_TIMER_FLAG_ENABLED;
                timer->flags &= ~MEDUSA_TIMER_FLAG_FIRED;
        } else {
                timer->flags &= ~MEDUSA_TIMER_FLAG_ENABLED;
                timer->flags &= ~MEDUSA_TIMER_FLAG_FIRED;
        }
        return 0;
}

static int timer_init_with_options_unlocked (struct medusa_timer *timer, const struct medusa_timer_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return -EINVAL;
        }
        memset(timer, 0, sizeof(struct medusa_timer));
        timer->onevent = options->onevent;
        timer->context = options->context;
        timer_set_initial(timer, options->initial);
        timer_set_interval(timer, options->interval);
        timer_set_accuracy(timer, options->accuracy);
        timer_set_resolution(timer, options->resolution);
        timer_set_singleshot(timer, options->singleshot);
        timer_set_enabled(timer, options->enabled);
        medusa_subject_set_type(&timer->subject, MEDUSA_SUBJECT_TYPE_TIMER);
        timer->subject.monitor = NULL;
        return medusa_monitor_add_unlocked(options->monitor, &timer->subject);
}

static void timer_uninit_unlocked (struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return;
        }
        if (timer->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&timer->subject);
        } else {
                medusa_timer_onevent_unlocked(timer, MEDUSA_TIMER_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) int medusa_timer_init_options_default (struct medusa_timer_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_timer_init_options));
        options->accuracy   = MEDUSA_TIMER_ACCURACY_DEFAULT;
        options->resolution = MEDUSA_TIMER_RESOLUTION_DEFAULT;
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_singleshot_unlocked (struct medusa_monitor *monitor, double interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, void *param), void *context)
{
        struct timespec timespec;
        if (monitor == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (interval < 0) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (onevent == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        timespec.tv_sec = (long long) interval;
        timespec.tv_nsec = (long long) ((interval - timespec.tv_sec) * 1e9);
        return medusa_timer_create_singleshot_timespec_unlocked(monitor, &timespec, onevent, context);
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_singleshot (struct medusa_monitor *monitor, double interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_timer *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_timer_create_singleshot_unlocked(monitor, interval, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_singleshot_timeval_unlocked (struct medusa_monitor *monitor, const struct timeval *interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, void *param), void *context)
{
        struct timespec timespec;
        if (monitor == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (interval == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (onevent == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        timespec.tv_sec = interval->tv_sec;
        timespec.tv_nsec = interval->tv_usec * 1e3;
        return medusa_timer_create_singleshot_timespec_unlocked(monitor, &timespec, onevent, context);
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_singleshot_timeval (struct medusa_monitor *monitor, const struct timeval *interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_timer *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_timer_create_singleshot_timeval_unlocked(monitor, interval, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_singleshot_timespec_unlocked (struct medusa_monitor *monitor, const struct timespec *interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_timer *timer;
        if (monitor == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (interval == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (onevent == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        timer = medusa_timer_create_unlocked(monitor, onevent, context);
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-EIO);
        }
        rc = medusa_timer_set_singleshot_unlocked(timer, 1);
        if (rc < 0) {
                medusa_timer_destroy_unlocked(timer);
                return MEDUSA_ERR_PTR(rc);
        }
        rc = medusa_timer_set_interval_timespec_unlocked(timer, interval);
        if (rc < 0) {
                medusa_timer_destroy_unlocked(timer);
                return MEDUSA_ERR_PTR(rc);
        }
        rc = medusa_timer_set_enabled_unlocked(timer, 1);
        if (rc < 0) {
                medusa_timer_destroy_unlocked(timer);
                return MEDUSA_ERR_PTR(rc);
        }
        timer->flags |= MEDUSA_TIMER_FLAG_AUTO_DESTROY;
        return timer;
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_singleshot_timespec (struct medusa_monitor *monitor, const struct timespec *interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_timer * rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_timer_create_singleshot_timespec_unlocked(monitor, interval, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_unlocked (struct medusa_monitor *monitor, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_timer_init_options options;
        rc = medusa_timer_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor = monitor;
        options.onevent = onevent;
        options.context = context;
        return medusa_timer_create_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_timer *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_timer_create_unlocked(monitor, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_with_options_unlocked (const struct medusa_timer_init_options *options)
{
        int rc;
        struct medusa_timer *timer;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_TIMER_USE_POOL) && (MEDUSA_TIMER_USE_POOL == 1)
        timer = medusa_pool_malloc(g_pool);
#else
        timer = malloc(sizeof(struct medusa_timer));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(timer, 0, sizeof(struct medusa_timer));
        rc = timer_init_with_options_unlocked(timer, options);
        if (rc < 0) {
                medusa_timer_destroy_unlocked(timer);
                return MEDUSA_ERR_PTR(rc);
        }
        return timer;
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create_with_options (const struct medusa_timer_init_options *options)
{
        struct medusa_timer *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_timer_create_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_timer_destroy_unlocked (struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return;
        }
        timer_uninit_unlocked(timer);
}

__attribute__ ((visibility ("default"))) void medusa_timer_destroy (struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return;
        }
        medusa_monitor_lock(timer->subject.monitor);
        medusa_timer_destroy_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial_unlocked (struct medusa_timer *timer, double initial)
{
        struct timespec timespec;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (initial < 0) {
                return -EINVAL;
        }
        timespec.tv_sec = (long long) initial;
        timespec.tv_nsec = (long long) ((initial - timespec.tv_sec) * 1e9);
        return medusa_timer_set_initial_timespec_unlocked(timer, &timespec);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial (struct medusa_timer *timer, double initial)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_initial_unlocked(timer, initial);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial_timeval_unlocked (struct medusa_timer *timer, const struct timeval *initial)
{
        struct timespec timespec;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(initial)) {
                return -EINVAL;
        }
        timespec.tv_sec = initial->tv_sec;
        timespec.tv_nsec = initial->tv_usec * 1e3;
        return medusa_timer_set_initial_timespec_unlocked(timer, &timespec);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial_timeval (struct medusa_timer *timer, const struct timeval *initial)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_initial_timeval_unlocked(timer, initial);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial_timespec_unlocked (struct medusa_timer *timer, const struct timespec *initial)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(initial)) {
                return -EINVAL;
        }
        rc = timer_set_initial_timespec(timer, initial);
        if (rc < 0) {
                return rc;
        }
        if (!medusa_timespec_isset(&timer->initial)) {
                timer->initial.tv_nsec = 1;
        }
        return medusa_monitor_mod_unlocked(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial_timespec (struct medusa_timer *timer, const struct timespec *initial)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_initial_timespec_unlocked(timer, initial);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_initial_unlocked (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        return timer->initial.tv_sec + timer->initial.tv_nsec * 1e-9;
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_initial (const struct medusa_timer *timer)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_initial_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval_unlocked (struct medusa_timer *timer, double interval)
{
        struct timespec timespec;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (interval < 0) {
                return -EINVAL;
        }
        timespec.tv_sec = (long long) interval;
        timespec.tv_nsec = (long long) ((interval - timespec.tv_sec) * 1e9);
        return medusa_timer_set_interval_timespec_unlocked(timer, &timespec);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval (struct medusa_timer *timer, double interval)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_interval_unlocked(timer, interval);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval_timeval_unlocked (struct medusa_timer *timer, const struct timeval *interval)
{
        struct timespec timespec;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(interval)) {
                return -EINVAL;
        }
        timespec.tv_sec = interval->tv_sec;
        timespec.tv_nsec = interval->tv_usec * 1e3;
        return medusa_timer_set_interval_timespec_unlocked(timer, &timespec);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval_timeval (struct medusa_timer *timer, const struct timeval *interval)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_interval_timeval_unlocked(timer, interval);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval_timespec_unlocked (struct medusa_timer *timer, const struct timespec *interval)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(interval)) {
                return -EINVAL;
        }
        rc = timer_set_interval_timespec(timer, interval);
        if (rc < 0) {
                return rc;
        }
        if (!medusa_timespec_isset(&timer->interval)) {
                timer->interval.tv_nsec = 1;
        }
        return medusa_monitor_mod_unlocked(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval_timespec (struct medusa_timer *timer, const struct timespec *interval)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_interval_timespec_unlocked(timer, interval);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_interval_unlocked (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        return timer->interval.tv_sec + timer->interval.tv_nsec * 1e-9;
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_interval (const struct medusa_timer *timer)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_interval_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_remaining_time_unlocked (const struct medusa_timer *timer)
{
        int rc;
        struct timespec rem;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (!medusa_timer_get_enabled_unlocked(timer)) {
                return -EAGAIN;
        }
        rc = medusa_timer_get_remaining_timespec_unlocked(timer, &rem);
        if (rc < 0) {
                return rc;
        }
        return rem.tv_sec + rem.tv_nsec * 1e-9;
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_remaining_time (const struct medusa_timer *timer)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_remaining_time_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_remaining_timeval_unlocked (const struct medusa_timer *timer, struct timeval *timeval)
{
        int rc;
        struct timespec rem;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (!medusa_timer_get_enabled_unlocked(timer)) {
                return -EAGAIN;
        }
        rc = medusa_timer_get_remaining_timespec_unlocked(timer, &rem);
        if (rc < 0) {
                return rc;
        }
        timeval->tv_sec = rem.tv_sec;
        timeval->tv_usec = (rem.tv_nsec + 500) / 1000;
        if (timeval->tv_usec >= 1000000) {
                timeval->tv_sec++;
                timeval->tv_usec -= 1000000;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_remaining_timeval (const struct medusa_timer *timer, struct timeval *timeval)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_remaining_timeval_unlocked(timer, timeval);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_remaining_timespec_unlocked (const struct medusa_timer *timer, struct timespec *timespec)
{
        int rc;
        struct timespec now;
        struct timespec rem;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (!medusa_timer_get_enabled_unlocked(timer)) {
                return -EAGAIN;
        }
        rc = medusa_clock_monotonic(&now);
        if (rc < 0) {
                return rc;
        }
        if (!medusa_timespec_compare(&timer->_timespec, &now, >)) {
                medusa_timespec_clear(timespec);
                return 0;
        }
        medusa_timespec_sub(&timer->_timespec, &now, &rem);
        timespec->tv_sec = rem.tv_sec;
        timespec->tv_nsec = rem.tv_nsec;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_remaining_timespec (const struct medusa_timer *timer, struct timespec *timespec)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_remaining_timespec_unlocked(timer, timespec);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_singleshot_unlocked (struct medusa_timer *timer, int singleshot)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        rc = timer_set_singleshot(timer, singleshot);
        if (rc < 0) {
                return rc;
        }
        return medusa_monitor_mod_unlocked(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_singleshot (struct medusa_timer *timer, int singleshot)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_singleshot_unlocked(timer, singleshot);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_singleshot_unlocked (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        return !!(timer->flags & MEDUSA_TIMER_FLAG_SINGLE_SHOT);
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_singleshot (const struct medusa_timer *timer)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_singleshot_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_accuracy_unlocked (struct medusa_timer *timer, unsigned int accuracy)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        rc = timer_set_accuracy(timer, accuracy);
        if (rc < 0) {
                return rc;
        }
        return medusa_monitor_mod_unlocked(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_accuracy (struct medusa_timer *timer, unsigned int accuracy)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_accuracy_unlocked(timer, accuracy);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) unsigned int medusa_timer_get_accuracy_unlocked (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (timer->flags & MEDUSA_TIMER_FLAG_NANOSECONDS) {
                return MEDUSA_TIMER_RESOLUTION_NANOSECONDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_MICROSECONDS) {
                return MEDUSA_TIMER_RESOLUTION_MICROSECONDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_MILLISECONDS) {
                return MEDUSA_TIMER_RESOLUTION_MILLISECONDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_SECONDS) {
                return MEDUSA_TIMER_RESOLUTION_SECONDS;
        }
        return MEDUSA_TIMER_RESOLUTION_DEFAULT;
}

__attribute__ ((visibility ("default"))) unsigned int medusa_timer_get_accuracy (const struct medusa_timer *timer)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_accuracy(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_resolution_unlocked (struct medusa_timer *timer, unsigned int resolution)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        rc = timer_set_resolution(timer, resolution);
        if (rc < 0) {
                return rc;
        }
        return medusa_monitor_mod_unlocked(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_resolution (struct medusa_timer *timer, unsigned int resolution)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_resolution_unlocked(timer, resolution);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) unsigned int medusa_timer_get_resolution_unlocked (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (timer->flags & MEDUSA_TIMER_FLAG_NANOSECONDS) {
                return MEDUSA_TIMER_RESOLUTION_NANOSECONDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_MICROSECONDS) {
                return MEDUSA_TIMER_RESOLUTION_MICROSECONDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_MILLISECONDS) {
                return MEDUSA_TIMER_RESOLUTION_MILLISECONDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_SECONDS) {
                return MEDUSA_TIMER_RESOLUTION_SECONDS;
        }
        return MEDUSA_TIMER_RESOLUTION_DEFAULT;
}

__attribute__ ((visibility ("default"))) unsigned int medusa_timer_get_resolution (const struct medusa_timer *timer)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_resolution(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_enabled_unlocked (struct medusa_timer *timer, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (!!medusa_timer_get_enabled_unlocked(timer) == !!enabled) {
                return 0;
        }
        rc = timer_set_enabled(timer, enabled);
        if (rc < 0) {
                return rc;
        }
        return medusa_monitor_mod_unlocked(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_enabled (struct medusa_timer *timer, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_enabled_unlocked(timer, enabled);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_enabled_unlocked (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        return !!(timer->flags & MEDUSA_TIMER_FLAG_ENABLED);
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_enabled (const struct medusa_timer *timer)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_enabled_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_enable (struct medusa_timer *timer)
{
        return medusa_timer_set_enabled(timer, 1);
}

__attribute__ ((visibility ("default"))) int medusa_timer_disable (struct medusa_timer *timer)
{
        return medusa_timer_set_enabled(timer, 0);
}

__attribute__ ((visibility ("default"))) int medusa_timer_restart (struct medusa_timer *timer)
{
        int rc;
        rc = medusa_timer_set_enabled(timer, 0);
        if (rc < 0) {
                return rc;
        }
        rc = medusa_timer_set_enabled(timer, 1);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_timer_start (struct medusa_timer *timer)
{
        return medusa_timer_set_enabled(timer, 1);
}

__attribute__ ((visibility ("default"))) int medusa_timer_stop (struct medusa_timer *timer)
{
        return medusa_timer_set_enabled(timer, 0);
}

__attribute__ ((visibility ("default"))) int medusa_timer_enable_unlocked (struct medusa_timer *timer)
{
        return medusa_timer_set_enabled_unlocked(timer, 1);
}

__attribute__ ((visibility ("default"))) int medusa_timer_disable_unlocked (struct medusa_timer *timer)
{
        return medusa_timer_set_enabled_unlocked(timer, 0);
}

__attribute__ ((visibility ("default"))) int medusa_timer_restart_unlocked (struct medusa_timer *timer)
{
        int rc;
        rc = medusa_timer_set_enabled_unlocked(timer, 0);
        if (rc < 0) {
                return rc;
        }
        rc = medusa_timer_set_enabled_unlocked(timer, 1);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_timer_start_unlocked (struct medusa_timer *timer)
{
        return medusa_timer_set_enabled_unlocked(timer, 1);
}

__attribute__ ((visibility ("default"))) int medusa_timer_stop_unlocked (struct medusa_timer *timer)
{
        return medusa_timer_set_enabled_unlocked(timer, 0);
}

__attribute__ ((visibility ("default"))) int medusa_timer_update_timespec_unlocked (struct medusa_timer *timer, struct timespec *now)
{
        unsigned int resolution;
        if (timer->flags & MEDUSA_TIMER_FLAG_FIRED) {
                if (timer->flags & MEDUSA_TIMER_FLAG_INTERVAL) {
                        medusa_timespec_add(now, &timer->interval, &timer->_timespec);
                } else if (timer->flags & MEDUSA_TIMER_FLAG_TICK) {
                        medusa_timespec_add(&timer->_timespec, &timer->interval, &timer->_timespec);
                } else if (timer->flags & MEDUSA_TIMER_FLAG_INCREMENTAL) {
                        medusa_timespec_add(now, &timer->interval, &timer->_timespec);
                } else {
                        medusa_timespec_add(now, &timer->interval, &timer->_timespec);
                }
        } else {
                if (medusa_timespec_isset(&timer->initial)) {
                        medusa_timespec_add(now, &timer->initial, &timer->_timespec);
                } else {
                        medusa_timespec_add(now, &timer->interval, &timer->_timespec);
                }
        }
        timer->flags &= ~MEDUSA_TIMER_FLAG_INITIAL;
        timer->flags &= ~MEDUSA_TIMER_FLAG_INTERVAL;
        if (!medusa_timespec_isset(&timer->_timespec)) {
                return -EIO;
        }
        resolution = medusa_timer_get_resolution_unlocked(timer);
        if (resolution == MEDUSA_TIMER_RESOLUTION_NANOSECONDS) {
        } else if (resolution == MEDUSA_TIMER_RESOLUTION_MICROSECONDS) {
                timer->_timespec.tv_nsec += 500;
                timer->_timespec.tv_nsec /= 1e3;
                timer->_timespec.tv_nsec *= 1e3;
        } else if (resolution == MEDUSA_TIMER_RESOLUTION_MILLISECONDS) {
                timer->_timespec.tv_nsec += 500000;
                timer->_timespec.tv_nsec /= 1e6;
                timer->_timespec.tv_nsec *= 1e6;
        } else if (resolution == MEDUSA_TIMER_RESOLUTION_SECONDS) {
                timer->_timespec.tv_nsec += 500000000;
                timer->_timespec.tv_nsec /= 1e9;
                timer->_timespec.tv_nsec *= 1e9;
        }
        if (timer->_timespec.tv_nsec >= 1000000000) {
                timer->_timespec.tv_sec++;
                timer->_timespec.tv_nsec -= 1000000000;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_context_unlocked (struct medusa_timer *timer, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        timer->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_context (struct medusa_timer *timer, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_context_unlocked(timer, context);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_timer_get_context_unlocked (struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return timer->context;
}

__attribute__ ((visibility ("default"))) void * medusa_timer_get_context (struct medusa_timer *timer)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_context_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_userdata_unlocked (struct medusa_timer *timer, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        timer->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_userdata (struct medusa_timer *timer, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_set_userdata_unlocked(timer, userdata);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_timer_get_userdata_unlocked (struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return timer->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_timer_get_userdata (struct medusa_timer *timer)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_userdata_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_userdata_ptr_unlocked (struct medusa_timer *timer, void *userdata)
{
        return medusa_timer_set_userdata_unlocked(timer, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_userdata_ptr (struct medusa_timer *timer, void *userdata)
{
        return medusa_timer_set_userdata(timer, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_timer_get_userdata_ptr_unlocked (struct medusa_timer *timer)
{
        return medusa_timer_get_userdata_unlocked(timer);
}

__attribute__ ((visibility ("default"))) void * medusa_timer_get_userdata_ptr (struct medusa_timer *timer)
{
        return medusa_timer_get_userdata(timer);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_userdata_int_unlocked (struct medusa_timer *timer, int userdata)
{
        return medusa_timer_set_userdata_unlocked(timer, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_userdata_int (struct medusa_timer *timer, int userdata)
{
        return medusa_timer_set_userdata(timer, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_userdata_int_unlocked (struct medusa_timer *timer)
{
        return (int) (intptr_t) medusa_timer_get_userdata_unlocked(timer);
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_userdata_int (struct medusa_timer *timer)
{
        return (int) (intptr_t) medusa_timer_get_userdata(timer);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_userdata_uint_unlocked (struct medusa_timer *timer, unsigned int userdata)
{
        return medusa_timer_set_userdata_unlocked(timer, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_userdata_uint (struct medusa_timer *timer, unsigned int userdata)
{
        return medusa_timer_set_userdata(timer, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_timer_get_userdata_uint_unlocked (struct medusa_timer *timer)
{
        return (unsigned int) (intptr_t) medusa_timer_get_userdata_unlocked(timer);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_timer_get_userdata_uint (struct medusa_timer *timer)
{
        return (unsigned int) (uintptr_t) medusa_timer_get_userdata(timer);
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_timer_get_monitor_unlocked (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return timer->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_timer_get_monitor (const struct medusa_timer *timer)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(timer->subject.monitor);
        rc = medusa_timer_get_monitor_unlocked(timer);
        medusa_monitor_unlock(timer->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_onevent_unlocked (struct medusa_timer *timer, unsigned int events, void *param)
{
        int rc;
        struct medusa_monitor *monitor;
        rc = 0;
        monitor = timer->subject.monitor;
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                timer->flags |= MEDUSA_TIMER_FLAG_FIRED;
                if (medusa_timer_get_singleshot_unlocked(timer) ||
                    !medusa_timespec_isset(&timer->interval)) {
                        rc = medusa_timer_set_enabled_unlocked(timer, 0);
                        if (rc < 0) {
                                return rc;
                        }
                }
        }
        if (timer->onevent != NULL) {
                if ((medusa_subject_is_active(&timer->subject)) ||
                    (events & MEDUSA_TIMER_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        rc = timer->onevent(timer, events, timer->context, param);
                        if (rc < 0) {
                                medusa_errorf("timer->onevent failed, rc: %d", rc);
                        }
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                if (timer->flags & MEDUSA_TIMER_FLAG_AUTO_DESTROY) {
                        medusa_timer_destroy_unlocked(timer);
                }
        }
        if (events & MEDUSA_TIMER_EVENT_DESTROY) {
#if defined(MEDUSA_TIMER_USE_POOL) && (MEDUSA_TIMER_USE_POOL == 1)
                medusa_pool_free(timer);
#else
                free(timer);
#endif
        }
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_timer_is_valid_unlocked (const struct medusa_timer *timer)
{
        if (!medusa_timespec_isset(&timer->initial) &&
            !medusa_timespec_isset(&timer->interval)) {
                return 0;
        }
        if ((timer->flags & MEDUSA_TIMER_FLAG_FIRED) &&
            !medusa_timespec_isset(&timer->interval)) {
                return 0;
        }
        if ((timer->flags & MEDUSA_TIMER_FLAG_ENABLED) == 0) {
                return 0;
        }
        return 1;
}

__attribute__ ((visibility ("default"))) const char * medusa_timer_event_string (unsigned int events)
{
        if (events == MEDUSA_TIMER_EVENT_TIMEOUT)               return "MEDUSA_TIMER_EVENT_TIMEOUT";
        if (events == MEDUSA_TIMER_EVENT_DESTROY)               return "MEDUSA_TIMER_EVENT_DESTROY";
        return "MEDUSA_TIMER_EVENT_UNKNOWN";
}

__attribute__ ((constructor)) static void timer_constructor (void)
{
#if defined(MEDUSA_TIMER_USE_POOL) && (MEDUSA_TIMER_USE_POOL == 1)
        g_pool = medusa_pool_create("medusa-timer", sizeof(struct medusa_timer), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void timer_destructor (void)
{
#if defined(MEDUSA_TIMER_USE_POOL) && (MEDUSA_TIMER_USE_POOL == 1)
        if (g_pool != NULL) {
                medusa_pool_destroy(g_pool);
        }
#endif
}
