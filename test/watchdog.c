
/*
 * linked into every test. a thread that ends a test that hangs, on every
 * platform, unlike alarm() which windows does not have.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "watchdog.h"

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_thread;
static int g_running;
static int g_stop;
static pid_t g_pid;
static double g_deadline;
static unsigned int g_seconds;
static const char *g_name;

static double watchdog_now (void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void watchdog_fire (void)
{
        fprintf(stderr, "watchdog: %s did not finish in %u seconds\n", (g_name != NULL) ? g_name : "test", g_seconds);
        fflush(stderr);
#if defined(__WINDOWS__)
        /* abort may bring up a dialog, nothing must wait for a click */
        _exit(3);
#else
        abort();
#endif
}

static void * watchdog_routine (void *arg)
{
        struct timespec ts;
        (void) arg;
        pthread_mutex_lock(&g_mutex);
        while (g_stop == 0) {
                if (g_deadline > 0 && watchdog_now() > g_deadline) {
                        watchdog_fire();
                }
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100 * 1000 * 1000;
                if (ts.tv_nsec >= 1000 * 1000 * 1000) {
                        ts.tv_sec  += 1;
                        ts.tv_nsec -= 1000 * 1000 * 1000;
                }
                pthread_cond_timedwait(&g_cond, &g_mutex, &ts);
        }
        pthread_mutex_unlock(&g_mutex);
        return NULL;
}

/* a running thread is reported as a leak by valgrind, stop it on exit */
static void watchdog_stop (void)
{
        if (g_running == 0 || getpid() != g_pid) {
                return;
        }
        pthread_mutex_lock(&g_mutex);
        g_stop = 1;
        pthread_cond_signal(&g_cond);
        pthread_mutex_unlock(&g_mutex);
        pthread_join(g_thread, NULL);
        g_running = 0;
}

static void watchdog_start (void)
{
#if !defined(__WINDOWS__)
        sigset_t all;
        sigset_t old;
        /* signals are for the test, signalfd needs them blocked in every thread */
        sigfillset(&all);
        pthread_sigmask(SIG_SETMASK, &all, &old);
#endif
        if (pthread_create(&g_thread, NULL, watchdog_routine, NULL) == 0) {
                g_running = 1;
                g_pid     = getpid();
                atexit(watchdog_stop);
        } else {
                fprintf(stderr, "watchdog: can not create thread\n");
        }
#if !defined(__WINDOWS__)
        pthread_sigmask(SIG_SETMASK, &old, NULL);
#endif
}

void watchdog_arm (unsigned int seconds, const char *name)
{
        pthread_once(&g_once, watchdog_start);
        pthread_mutex_lock(&g_mutex);
        g_seconds  = seconds;
        g_name     = name;
        g_deadline = (seconds > 0) ? watchdog_now() + seconds : 0;
        pthread_mutex_unlock(&g_mutex);
}
