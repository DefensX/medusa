
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "clock.h"
#include "error.h"
#include "pool.h"
#include "queue.h"
#include "time.h"
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
        MEDUSA_TIMER_FLAG_ENABLED       = 0x00000001,
        MEDUSA_TIMER_FLAG_SINGLE_SHOT   = 0x00000002,
        MEDUSA_TIMER_FLAG_NANOSECONDS   = 0x00000004,
        MEDUSA_TIMER_FLAG_MICROSECONDS  = 0x00000008,
        MEDUSA_TIMER_FLAG_MILLISECONDS  = 0x00000010,
        MEDUSA_TIMER_FLAG_SECONDS       = 0x00000020
#define MEDUSA_TIMER_FLAG_ENABLED       MEDUSA_TIMER_FLAG_ENABLED
#define MEDUSA_TIMER_FLAG_SINGLE_SHOT   MEDUSA_TIMER_FLAG_SINGLE_SHOT
#define MEDUSA_TIMER_FLAG_NANOSECONDS   MEDUSA_TIMER_FLAG_NANOSECONDS
#define MEDUSA_TIMER_FLAG_MICROSECONDS  MEDUSA_TIMER_FLAG_MICROSECONDS
#define MEDUSA_TIMER_FLAG_MILLISECONDS  MEDUSA_TIMER_FLAG_MILLISECONDS
#define MEDUSA_TIMER_FLAG_SECONDS       MEDUSA_TIMER_FLAG_SECONDS
};

__attribute__ ((visibility ("default"))) int medusa_timer_init (struct medusa_monitor *monitor, struct medusa_timer *timer, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context), void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(onevent)) {
                return -EINVAL;
        }
        memset(timer, 0, sizeof(struct medusa_timer));
        timer->onevent = onevent;
        timer->context = context;
        timer->flags |= MEDUSA_TIMER_FLAG_MILLISECONDS;
        medusa_timespec_clear(&timer->initial);
        medusa_timespec_clear(&timer->interval);
        timer->subject.flags = MEDUSA_SUBJECT_FLAG_TIMER;
        timer->subject.monitor = NULL;
        return medusa_monitor_add(monitor, &timer->subject);
}

__attribute__ ((visibility ("default"))) void medusa_timer_uninit (struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return;
        }
        if ((timer->subject.flags & MEDUSA_SUBJECT_FLAG_TIMER) == 0) {
                return;
        }
        if (timer->subject.monitor != NULL) {
                medusa_monitor_del(&timer->subject);
        } else {
                medusa_timer_onevent(timer, MEDUSA_TIMER_EVENT_DESTROY);
        }
}

__attribute__ ((visibility ("default"))) struct medusa_timer * medusa_timer_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context), void *context)
{
        int rc;
        struct medusa_timer *timer;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(onevent)) {
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
        rc = medusa_timer_init(monitor, timer, onevent, context);
        if (rc < 0) {
                medusa_timer_destroy(timer);
                return MEDUSA_ERR_PTR(rc);
        }
        timer->subject.flags |= MEDUSA_SUBJECT_FLAG_ALLOC;
        return timer;
}

__attribute__ ((visibility ("default"))) void medusa_timer_destroy (struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return;
        }
        medusa_timer_uninit(timer);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial (struct medusa_timer *timer, double initial)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                goto bail;
        }
        timer->initial.tv_sec = (long long) initial;
        timer->initial.tv_nsec = (long long) ((initial - timer->initial.tv_sec) * 1e9);
        return medusa_monitor_mod(&timer->subject);
bail:   return -1;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial_timeval (struct medusa_timer *timer, const struct timeval *timeval)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(timeval)) {
                return -EINVAL;
        }
        timer->initial.tv_sec = timeval->tv_sec;
        timer->initial.tv_nsec = timeval->tv_usec * 1e3;
        return medusa_monitor_mod(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_initial_timespec (struct medusa_timer *timer, const struct timespec *timespec)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(timespec)) {
                return -EINVAL;
        }
        timer->initial.tv_sec = timespec->tv_sec;
        timer->initial.tv_nsec = timespec->tv_nsec;
        return medusa_monitor_mod(&timer->subject);
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_initial (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        return timer->initial.tv_sec + timer->initial.tv_nsec * 1e-9;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval (struct medusa_timer *timer, double interval)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        timer->interval.tv_sec = (long long) interval;
        timer->interval.tv_nsec = (long long) ((interval - timer->interval.tv_sec) * 1e9);
        return medusa_monitor_mod(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval_timeval (struct medusa_timer *timer, const struct timeval *timeval)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(timeval)) {
                return -EINVAL;
        }
        timer->interval.tv_sec = timeval->tv_sec;
        timer->interval.tv_nsec = timeval->tv_usec * 1e3;
        return medusa_monitor_mod(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_interval_timespec (struct medusa_timer *timer, const struct timespec *timespec)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(timespec)) {
                return -EINVAL;
        }
        timer->interval.tv_sec = timespec->tv_sec;
        timer->interval.tv_nsec = timespec->tv_nsec;
        return medusa_monitor_mod(&timer->subject);
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_interval (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        return timer->interval.tv_sec + timer->interval.tv_nsec * 1e-9;
}

__attribute__ ((visibility ("default"))) double medusa_timer_get_remaining_time (const struct medusa_timer *timer)
{
        struct timespec now;
        struct timespec rem;
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        medusa_clock_monotonic(&now);
        medusa_timespec_sub(&timer->_timespec, &now, &rem);
        return rem.tv_sec + rem.tv_nsec + 1e-9;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_single_shot (struct medusa_timer *timer, int single_shot)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (single_shot) {
                timer->flags |= MEDUSA_TIMER_FLAG_SINGLE_SHOT;
        } else {
                timer->flags &= ~MEDUSA_TIMER_FLAG_SINGLE_SHOT;
        }
        return medusa_monitor_mod(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_single_shot (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        return !!(timer->flags & MEDUSA_TIMER_FLAG_SINGLE_SHOT);
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_resolution (struct medusa_timer *timer, unsigned int resolution)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        timer->flags &= ~MEDUSA_TIMER_FLAG_NANOSECONDS;
        timer->flags &= ~MEDUSA_TIMER_FLAG_MICROSECONDS;
        timer->flags &= ~MEDUSA_TIMER_FLAG_MILLISECONDS;
        timer->flags &= ~MEDUSA_TIMER_FLAG_SECONDS;
        if (resolution == MEDUSA_TIMER_RESOLUTION_NANOSECOMDS) {
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
        return medusa_monitor_mod(&timer->subject);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_timer_get_resolution (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (timer->flags & MEDUSA_TIMER_FLAG_NANOSECONDS) {
                return MEDUSA_TIMER_RESOLUTION_NANOSECOMDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_MICROSECONDS) {
                return MEDUSA_TIMER_RESOLUTION_MICROSECONDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_MILLISECONDS) {
                return MEDUSA_TIMER_RESOLUTION_MILLISECONDS;
        } else if (timer->flags & MEDUSA_TIMER_FLAG_SECONDS) {
                return MEDUSA_TIMER_RESOLUTION_SECONDS;
        }
        return MEDUSA_TIMER_RESOLUTION_DEFAULT;
}

__attribute__ ((visibility ("default"))) int medusa_timer_set_enabled (struct medusa_timer *timer, int enabled)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        if (enabled) {
                timer->flags |= MEDUSA_TIMER_FLAG_ENABLED;
        } else {
                timer->flags &= ~MEDUSA_TIMER_FLAG_ENABLED;
        }
        return medusa_monitor_mod(&timer->subject);
}

__attribute__ ((visibility ("default"))) int medusa_timer_get_enabled (const struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return -EINVAL;
        }
        return !!(timer->flags & MEDUSA_TIMER_FLAG_ENABLED);
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_timer_get_monitor (struct medusa_timer *timer)
{
        if (MEDUSA_IS_ERR_OR_NULL(timer)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return timer->subject.monitor;
}

int medusa_timer_onevent (struct medusa_timer *timer, unsigned int events)
{
        int rc;
        rc = 0;
        if (timer->onevent != NULL) {
                rc = timer->onevent(timer, events, timer->context);
        }
        if ((rc != 1) &&
            (events & MEDUSA_TIMER_EVENT_DESTROY)) {
                if (timer->subject.flags & MEDUSA_SUBJECT_FLAG_ALLOC) {
#if defined(MEDUSA_TIMER_USE_POOL) && (MEDUSA_TIMER_USE_POOL == 1)
                        medusa_pool_free(timer);
#else
                        free(timer);
#endif
                } else {
                        memset(timer, 0, sizeof(struct medusa_timer));
                }
        }
        return rc;
}

int medusa_timer_is_valid (const struct medusa_timer *timer)
{
        if (!medusa_timespec_isset(&timer->initial) &&
            !medusa_timespec_isset(&timer->interval)) {
                return 0;
        }
        if ((timer->flags & MEDUSA_TIMER_FLAG_ENABLED) == 0) {
                return 0;
        }
        return 1;
}

__attribute__ ((constructor)) static void timer_constructor (void)
{
#if defined(MEDUSA_TIMER_USE_POOL) && (MEDUSA_TIMER_USE_POOL == 1)
        g_pool = medusa_pool_create("medusa-timer", sizeof(struct medusa_timer), 0, 0, MEDUSA_POOL_FLAG_DEFAULT, NULL, NULL, NULL);
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
