
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#include "debug.h"

int medusa_debug_level                          = MEDUSA_DEBUG_LEVEL_ERROR;
static pthread_mutex_t medusa_debug_mutex       = PTHREAD_MUTEX_INITIALIZER;

static char *debug_buffer       = NULL;
static int debug_buffer_size    = 0;

void medusa_debug_lock (void)
{
        pthread_mutex_lock(&medusa_debug_mutex);
}

void medusa_debug_unlock (void)
{
        pthread_mutex_unlock(&medusa_debug_mutex);
}

const char * medusa_debug_level_to_string (int level)
{
        switch (level) {
                case MEDUSA_DEBUG_LEVEL_SILENT: return "silent";
                case MEDUSA_DEBUG_LEVEL_ERROR: return "error";
                case MEDUSA_DEBUG_LEVEL_WARNING: return "warning";
                case MEDUSA_DEBUG_LEVEL_NOTICE: return "notice";
                case MEDUSA_DEBUG_LEVEL_INFO: return "info";
                case MEDUSA_DEBUG_LEVEL_DEBUG: return "debug";
                case MEDUSA_DEBUG_LEVEL_TRACE: return "trace";
        }
        return "unknown";
}

int medusa_debug_level_from_string (const char *string)
{
        if (string == NULL) {
                return MEDUSA_DEBUG_LEVEL_ERROR;
        }
        if (strcmp(string, "silent") == 0 || strcmp(string, "s") == 0) {
                return MEDUSA_DEBUG_LEVEL_SILENT;
        }
        if (strcmp(string, "error") == 0 || strcmp(string, "e") == 0) {
                return MEDUSA_DEBUG_LEVEL_ERROR;
        }
        if (strcmp(string, "warning") == 0 || strcmp(string, "w") == 0) {
                return MEDUSA_DEBUG_LEVEL_WARNING;
        }
        if (strcmp(string, "notice") == 0 || strcmp(string, "n") == 0) {
                return MEDUSA_DEBUG_LEVEL_NOTICE;
        }
        if (strcmp(string, "info") == 0 || strcmp(string, "i") == 0) {
                return MEDUSA_DEBUG_LEVEL_INFO;
        }
        if (strcmp(string, "debug") == 0 || strcmp(string, "d") == 0) {
                return MEDUSA_DEBUG_LEVEL_DEBUG;
        }
        if (strcmp(string, "trace") == 0 || strcmp(string, "t") == 0) {
                return MEDUSA_DEBUG_LEVEL_TRACE;
        }
        return MEDUSA_DEBUG_LEVEL_ERROR;
}

int medusa_debug_printf (int level, const char *name, const char *function, const char *file, int line, const char *fmt, ...)
{
        int rc;
        va_list ap;

        struct timeval timeval;
        struct tm *tm;
        time_t seconds;
        int milliseconds;
        char date[80];

        medusa_debug_lock();

        va_start(ap, fmt);
        rc = vsnprintf(debug_buffer, debug_buffer_size, fmt, ap);
        va_end(ap);
        if (rc < 0) {
                medusa_debug_unlock();
                goto bail;
        }
        if (debug_buffer_size == 0 ||
            rc >= debug_buffer_size) {
                free(debug_buffer);
                debug_buffer = malloc(rc + 1);
                if (debug_buffer == NULL) {
                        goto bail;
                }
                debug_buffer_size = rc + 1;
                va_start(ap, fmt);
                rc = vsnprintf(debug_buffer, debug_buffer_size, fmt, ap);
                va_end(ap);
                if (rc < 0) {
                        medusa_debug_unlock();
                        goto bail;
                }
        }

        gettimeofday(&timeval, NULL);

        milliseconds = (int) ((timeval.tv_usec / 1000.0) + 0.5);
        if (milliseconds >= 1000) {
                milliseconds -= 1000;
                timeval.tv_sec++;
        }
        seconds = timeval.tv_sec;
        tm = localtime(&seconds);
        strftime(date, sizeof(date), "%x-%H:%M:%S", tm);

        fprintf(stderr, "medusa:%s.%03d:%-8s:%-6s: %s (%s %s:%d)\n", date, milliseconds, name, medusa_debug_level_to_string(level), debug_buffer, function, file, line);
        fflush(stderr);

        medusa_debug_unlock();

        return 0;
bail:   va_end(ap);
        return -1;
}

__attribute__((destructor)) int medusa_debug_fini (void)
{
        if (debug_buffer != NULL) {
                free(debug_buffer);
        }
        return 0;
}
