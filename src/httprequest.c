
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "error.h"
#include "pool.h"
#include "queue.h"
#include "buffer.h"
#include "subject-struct.h"
#include "tcpsocket.h"
#include "tcpsocket-private.h"
#include "httprequest.h"
#include "httprequest-private.h"
#include "httprequest-struct.h"
#include "monitor-private.h"

#define MIN(a, b)                               (((a) < (b)) ? (a) : (b))

#define MEDUSA_HTTPREQUEST_USE_POOL               1

#define MEDUSA_HTTPREQUEST_STATE_MASK             0xff
#define MEDUSA_HTTPREQUEST_STATE_SHIFT            0x18

#if defined(MEDUSA_HTTPREQUEST_USE_POOL) && (MEDUSA_HTTPREQUEST_USE_POOL == 1)
static struct medusa_pool *g_pool;
#endif

struct medusa_url {
        char *base;
        char *host;
        unsigned short port;
        char *path;
};

static void medusa_url_uninit (struct medusa_url *url)
{
        if (url == NULL) {
                return;
        }
        if (url->base != NULL) {
                free(url->base);
        }
        memset(url, 0, sizeof(struct medusa_url));
}

static int medusa_url_init (struct medusa_url *url, const char *uri)
{
        char *i;
        char *p;
        char *e;
        char *t;
        memset(url, 0, sizeof(struct medusa_url));
        url->base = strdup(uri);
        if (url->base == NULL) {
                return -ENOMEM;
        }
        if (url->base[0] == '<') {
                memmove(url->base, url->base + 1, strlen(url->base) - 1);
                t = strchr(url->base, '>');
                if (t != NULL) {
                        *t = '\0';
                }
        }
        if (strncasecmp(url->base, "http://", 7) == 0) {
                i = url->base + 7;
        } else {
                i = url->base;
        }
        p = strchr(i, ':');
        e = strchr(i, '/');
        if (p == NULL || e < p) {
                url->port = 80;
                url->host = i;
                if (e != NULL) {
                        *e = '\0';
                }
        } else if (p != NULL) {
                url->port = atoi(p + 1);
                url->host = i;
                *p = '\0';
                if (e != NULL) {
                        *e = '\0';
                }
        }
        if (e != NULL) {
                do {
                        e++;
                } while (*e == '/');
                url->path = e;
        }
        if (url->host == NULL ||
            url->port == 0) {
                medusa_url_uninit(url);
                return -EINVAL;
        }
        return 0;
}

static int httprequest_tcpsocket_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, ...)
{
        (void) tcpsocket;
        (void) context;

        fprintf(stderr, "events: 0x%08x\n", events);

        return 0;
}

static inline unsigned int httprequest_get_state (const struct medusa_httprequest *httprequest)
{
        return httprequest->state;
}

static inline int httprequest_set_state (struct medusa_httprequest *httprequest, unsigned int state)
{
        if (state == MEDUSA_TCPSOCKET_STATE_DISCONNECTED) {
                if (!MEDUSA_IS_ERR_OR_NULL(httprequest->tcpsocket)) {
                        medusa_tcpsocket_destroy_unlocked(httprequest->tcpsocket);
                        httprequest->tcpsocket = NULL;
                }
        }
        httprequest->state = state;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_init_options_default (struct medusa_httprequest_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_httprequest_init_options));
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_init_unlocked (struct medusa_httprequest *httprequest, struct medusa_monitor *monitor, int (*onevent) (struct medusa_httprequest *httprequest, unsigned int events, void *context, ...), void *context)
{
        int rc;
        struct medusa_httprequest_init_options options;
        rc = medusa_httprequest_init_options_default(&options);
        if (rc < 0) {
                return rc;
        }
        options.monitor = monitor;
        options.onevent = onevent;
        options.context = context;
        return medusa_httprequest_init_with_options_unlocked(httprequest, &options);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_init (struct medusa_httprequest *httprequest, struct medusa_monitor *monitor, int (*onevent) (struct medusa_httprequest *httprequest, unsigned int events, void *context, ...), void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(monitor);
        rc = medusa_httprequest_init_unlocked(httprequest, monitor, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_init_with_options_unlocked (struct medusa_httprequest *httprequest, const struct medusa_httprequest_init_options *options)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
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
        memset(httprequest, 0, sizeof(struct medusa_httprequest));
        medusa_subject_set_type(&httprequest->subject, MEDUSA_SUBJECT_TYPE_HTTPREQUEST);
        httprequest->subject.monitor = NULL;
        httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
        httprequest->onevent = options->onevent;
        httprequest->context = options->context;
        httprequest->hbuffer = medusa_buffer_create(MEDUSA_BUFFER_TYPE_DEFAULT);
        if (MEDUSA_IS_ERR_OR_NULL(httprequest->hbuffer)) {
                return MEDUSA_PTR_ERR(httprequest->hbuffer);
        }
        rc = medusa_monitor_add_unlocked(options->monitor, &httprequest->subject);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_init_with_options (struct medusa_httprequest *httprequest, const struct medusa_httprequest_init_options *options)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_httprequest_init_with_options_unlocked(httprequest, options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_httprequest_uninit_unlocked (struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return;
        }
        if (httprequest->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&httprequest->subject);
        } else {
                medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_DESTROY);
        }
}

__attribute__ ((visibility ("default"))) void medusa_httprequest_uninit (struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        medusa_httprequest_uninit_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
}

__attribute__ ((visibility ("default"))) struct medusa_httprequest * medusa_httprequest_create_unlocked (struct medusa_monitor *monitor, int (*onevent) (struct medusa_httprequest *httprequest, unsigned int events, void *context, ...), void *context)
{
        int rc;
        struct medusa_httprequest_init_options options;
        rc = medusa_httprequest_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor = monitor;
        options.onevent = onevent;
        options.context = context;
        return medusa_httprequest_create_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_httprequest * medusa_httprequest_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_httprequest *httprequest, unsigned int events, void *context, ...), void *context)
{
        struct medusa_httprequest *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_httprequest_create_unlocked(monitor, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_httprequest * medusa_httprequest_create_with_options_unlocked (const struct medusa_httprequest_init_options *options)
{
        int rc;
        struct medusa_httprequest *httprequest;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_HTTPREQUEST_USE_POOL) && (MEDUSA_HTTPREQUEST_USE_POOL == 1)
        httprequest = medusa_pool_malloc(g_pool);
#else
        httprequest = malloc(sizeof(struct medusa_httprequest));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(httprequest, 0, sizeof(struct medusa_httprequest));
        rc = medusa_httprequest_init_with_options_unlocked(httprequest, options);
        if (rc < 0) {
                medusa_httprequest_destroy_unlocked(httprequest);
                return MEDUSA_ERR_PTR(rc);
        }
        httprequest->subject.flags |= MEDUSA_SUBJECT_FLAG_ALLOC;
        return httprequest;
}

__attribute__ ((visibility ("default"))) struct medusa_httprequest * medusa_httprequest_create_with_options (const struct medusa_httprequest_init_options *options)
{
        struct medusa_httprequest *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_httprequest_create_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_httprequest_destroy_unlocked (struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return;
        }
        medusa_httprequest_uninit_unlocked(httprequest);
}

__attribute__ ((visibility ("default"))) void medusa_httprequest_destroy (struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        medusa_httprequest_destroy_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_httprequest_get_state_unlocked (const struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_HTTPREQUEST_STATE_UNKNWON;
        }
        return httprequest_get_state(httprequest);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_httprequest_get_state (const struct medusa_httprequest *httprequest)
{
        unsigned int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_HTTPREQUEST_STATE_UNKNWON;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_state_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_connect_timeout_unlocked (struct medusa_httprequest *httprequest, double timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        (void) timeout;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_connect_timeout (struct medusa_httprequest *httprequest, double timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_set_connect_timeout_unlocked(httprequest, timeout);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_httprequest_get_connect_timeout_unlocked (const struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        return -EIO;
}

__attribute__ ((visibility ("default"))) double medusa_httprequest_get_connect_timeout (const struct medusa_httprequest *httprequest)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_connect_timeout(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_read_timeout_unlocked (struct medusa_httprequest *httprequest, double timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        (void) timeout;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_read_timeout (struct medusa_httprequest *httprequest, double timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_set_read_timeout_unlocked(httprequest, timeout);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_httprequest_get_read_timeout_unlocked (const struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        return -EIO;
}

__attribute__ ((visibility ("default"))) double medusa_httprequest_get_read_timeout (const struct medusa_httprequest *httprequest)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_read_timeout(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_header_unlocked (struct medusa_httprequest *httprequest, const char *key, const char *value, ...)
{
        int64_t rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(key)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(value)) {
                return -EINVAL;
        }
        va_start(va, value);
        rc = medusa_httprequest_add_vheader_unlocked(httprequest, key, value, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_header (struct medusa_httprequest *httprequest, const char *key, const char *value, ...)
{
        int64_t rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(key)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(value)) {
                return -EINVAL;
        }
        va_start(va, value);
        rc = medusa_httprequest_add_vheader(httprequest, key, value, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_vheader_unlocked (struct medusa_httprequest *httprequest, const char *key, const char *value, va_list va)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(key)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(value)) {
                return -EINVAL;
        }
        rc  = medusa_buffer_printf(httprequest->hbuffer, "%s: ", key);
        if (rc < 0) {
                return rc;
        }
        rc |= medusa_buffer_vprintf(httprequest->hbuffer, value, va);
        if (rc < 0) {
                return rc;
        }
        rc |= medusa_buffer_printf(httprequest->hbuffer, "\r\n");
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_vheader (struct medusa_httprequest *httprequest, const char *key, const char *value, va_list va)
{
        int64_t rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_add_vheader_unlocked(httprequest, key, value, va);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_post_unlocked (struct medusa_httprequest *httprequest, const char *url, const void *data, int64_t length)
{
        int rc;
        int ret;
        struct medusa_url medusa_url;
        struct medusa_tcpsocket_init_options medusa_tcpsocket_init_options;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (url == NULL) {
                return -EINVAL;
        }
        if (length < 0) {
                return -EINVAL;
        }
        if (length != 0) {
                if (data == NULL) {
                        return -EINVAL;
                }
        }
        if (httprequest_get_state(httprequest) != MEDUSA_HTTPREQUEST_STATE_DISCONNECTED) {
                return -EINVAL;
        }
        rc = medusa_url_init(&medusa_url, url);
        if (rc < 0) {
                return rc;
        }
        ret = 0;
        rc = medusa_tcpsocket_init_options_default(&medusa_tcpsocket_init_options);
        if (rc < 0) {
                ret = rc;
                goto bail;
        }
        medusa_tcpsocket_init_options.monitor     = httprequest->subject.monitor;
        medusa_tcpsocket_init_options.onevent     = httprequest_tcpsocket_onevent;
        medusa_tcpsocket_init_options.context     = httprequest;
        medusa_tcpsocket_init_options.nonblocking = 1;
        medusa_tcpsocket_init_options.enabled     = 1;
        httprequest->tcpsocket = medusa_tcpsocket_create_with_options_unlocked(&medusa_tcpsocket_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(httprequest->tcpsocket)) {
                ret = MEDUSA_PTR_ERR(httprequest->tcpsocket);
                goto bail;
        }
        rc = medusa_tcpsocket_connect_unlocked(httprequest->tcpsocket, MEDUSA_TCPSOCKET_PROTOCOL_ANY, medusa_url.host, medusa_url.port);
        if (rc < 0) {
                ret = rc;
                goto bail;
        }
        medusa_url_uninit(&medusa_url);
        return 0;
bail:   medusa_url_uninit(&medusa_url);
        httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
        medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED);
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_post (struct medusa_httprequest *httprequest, const char *url, const void *data, int64_t length)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_make_post_unlocked(httprequest, url, data, length);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_onevent_unlocked (struct medusa_httprequest *httprequest, unsigned int events)
{
        int ret;
        struct medusa_monitor *monitor;
        ret = 0;
        monitor = httprequest->subject.monitor;
        if (httprequest->onevent != NULL) {
                medusa_monitor_unlock(monitor);
                ret = httprequest->onevent(httprequest, events, httprequest->context);
                medusa_monitor_lock(monitor);
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_DESTROY) {
                if (!MEDUSA_IS_ERR_OR_NULL(httprequest->hbuffer)) {
                        medusa_buffer_destroy(httprequest->hbuffer);
                        httprequest->hbuffer = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(httprequest->tcpsocket)) {
                        medusa_tcpsocket_destroy_unlocked(httprequest->tcpsocket);
                        httprequest->tcpsocket = NULL;
                }
                if (httprequest->subject.flags & MEDUSA_SUBJECT_FLAG_ALLOC) {
#if defined(MEDUSA_HTTPREQUEST_USE_POOL) && (MEDUSA_HTTPREQUEST_USE_POOL == 1)
                        medusa_pool_free(httprequest);
#else
                        free(httprequest);
#endif
                } else {
                        memset(httprequest, 0, sizeof(struct medusa_httprequest));
                }
        }
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_onevent (struct medusa_httprequest *httprequest, unsigned int events)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_onevent_unlocked(httprequest, events);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_httprequest_get_monitor_unlocked (struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return httprequest->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_httprequest_get_monitor (struct medusa_httprequest *httprequest)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_monitor_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((constructor)) static void httprequest_constructor (void)
{
#if defined(MEDUSA_HTTPREQUEST_USE_POOL) && (MEDUSA_HTTPREQUEST_USE_POOL == 1)
        g_pool = medusa_pool_create("medusa-httprequest", sizeof(struct medusa_httprequest), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void httprequest_destructor (void)
{
#if defined(MEDUSA_HTTPREQUEST_USE_POOL) && (MEDUSA_HTTPREQUEST_USE_POOL == 1)
        if (g_pool != NULL) {
                medusa_pool_destroy(g_pool);
        }
#endif
}
