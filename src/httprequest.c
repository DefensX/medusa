
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include <inttypes.h>
#include <sys/types.h>

#define MEDUSA_DEBUG_NAME       "httprequest"

#include "../3rdparty/http-parser/http_parser.h"

#include "strndup.h"
#include "debug.h"
#include "error.h"
#include "pool.h"
#include "queue.h"
#include "iovec.h"
#include "buffer.h"
#include "subject-struct.h"
#include "tcpsocket.h"
#include "tcpsocket-private.h"
#include "httprequest.h"
#include "httprequest-private.h"
#include "httprequest-struct.h"
#include "monitor-private.h"

#if !defined(MIN)
#define MIN(a, b)                               (((a) < (b)) ? (a) : (b))
#endif

#define MEDUSA_HTTPREQUEST_USE_POOL             1

#if defined(MEDUSA_HTTPREQUEST_USE_POOL) && (MEDUSA_HTTPREQUEST_USE_POOL == 1)
static struct medusa_pool *g_pool;
#endif

struct url {
        char *base;
        char *scheme;
        char *host;
        unsigned short port;
        char *path;
        int ssl;
};

static void url_uninit (struct url *url)
{
        if (url == NULL) {
                return;
        }
        if (url->base != NULL) {
                free(url->base);
        }
        memset(url, 0, sizeof(struct url));
}

static int url_parse (struct url *url, const char *uri)
{
        char *i;
        char *s;
        char *p;
        char *e;
        char *t;

        if (uri == NULL) {
                return -EINVAL;
        }
        memset(url, 0, sizeof(struct url));

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

        i = url->base;

        s = strstr(url->base, "://");
        e = strchr(i, '/');
        if (s == NULL || e < s) {
                url->scheme = NULL;
        } else {
                url->scheme = i;
                *(e - 1) = '\0';
                i = s + 3;

                if (strcasecmp(url->scheme, "http") == 0) {
                        url->port = 80;
                } else if (strcasecmp(url->scheme, "https") == 0) {
                        url->port = 443;
                        url->ssl  = 1;
                }
        }

        p = strchr(i, ':');
        e = strchr(i, '/');
        if (p == NULL || e < p) {
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

        if (url->host == NULL) {
                url_uninit(url);
                return -EINVAL;
        }

        return 0;
}

TAILQ_HEAD(medusa_httprequest_reply_headers_list, medusa_httprequest_reply_header);
struct medusa_httprequest_reply_header {
        TAILQ_ENTRY(medusa_httprequest_reply_header) list;
        char *key;
        char *value;
};

struct medusa_httprequest_reply_status {
        unsigned int code;
        char *value;
};

struct medusa_httprequest_reply_headers {
        int64_t count;
        struct medusa_httprequest_reply_headers_list list;
};

struct medusa_httprequest_reply_body {
        int64_t length;
        void *value;
};

struct medusa_httprequest_reply {
        struct medusa_httprequest_reply_status status;
        struct medusa_httprequest_reply_headers headers;
        struct medusa_httprequest_reply_body body;
};

static int medusa_httprequest_reply_header_set_key (struct medusa_httprequest_reply_header *header, const char *key, int64_t length)
{
        if (header == NULL) {
                return -EINVAL;
        }
        if (key == NULL) {
                return -EINVAL;
        }
        if (length <= 0) {
                return -EINVAL;
        }
        if (header->key != NULL) {
                char *tmp = realloc(header->key, strlen(header->key) + length + 1);
                if (tmp == NULL) {
                        return -ENOMEM;
                }
                header->key = tmp;
                strncat(header->key, key, length);
        } else {
                header->key = medusa_strndup(key, length);
                if (header->key == NULL) {
                        return -ENOMEM;
                }
        }
        return 0;
}

static int medusa_httprequest_reply_header_set_value (struct medusa_httprequest_reply_header *header, const char *value, int64_t length)
{
        if (header == NULL) {
                return -EINVAL;
        }
        if (value == NULL) {
                return -EINVAL;
        }
        if (length <= 0) {
                return -EINVAL;
        }
        if (header->value != NULL) {
                char *tmp = realloc(header->value, strlen(header->value) + length + 1);
                if (tmp == NULL) {
                        return -ENOMEM;
                }
                header->value = tmp;
                strncat(header->value, value, length);
        } else {
                header->value = medusa_strndup(value, length);
                if (header->value == NULL) {
                        return -ENOMEM;
                }
        }
        return 0;
}

static void medusa_httprequest_reply_header_destroy (struct medusa_httprequest_reply_header *header)
{
        if (header == NULL) {
                return;
        }
        if (header->key != NULL) {
                free(header->key);
        }
        if (header->value != NULL) {
                free(header->value);
        }
        free(header);
}

static struct medusa_httprequest_reply_header * medusa_httprequest_reply_header_create (void)
{
        struct medusa_httprequest_reply_header *header;
        header = malloc(sizeof(struct medusa_httprequest_reply_header));
        if (header == NULL) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(header, 0, sizeof(struct medusa_httprequest_reply_header));
        return header;
}

static void medusa_httprequest_reply_body_uninit (struct medusa_httprequest_reply_body *body)
{
        body->length = 0;
        if (body->value != NULL) {
                free(body->value);
        }
}

static void medusa_httprequest_reply_body_init (struct medusa_httprequest_reply_body *body)
{
        memset(body, 0, sizeof(struct medusa_httprequest_reply_body));
        body->length = 0;
}

static void medusa_httprequest_reply_headers_uninit (struct medusa_httprequest_reply_headers *headers)
{
        struct medusa_httprequest_reply_header *header;
        struct medusa_httprequest_reply_header *nheader;
        TAILQ_FOREACH_SAFE(header, &headers->list, list, nheader) {
                TAILQ_REMOVE(&headers->list, header, list);
                medusa_httprequest_reply_header_destroy(header);
        }
        headers->count = 0;
}

static void medusa_httprequest_reply_headers_init (struct medusa_httprequest_reply_headers *headers)
{
        memset(headers, 0, sizeof(struct medusa_httprequest_reply_headers));
        headers->count = 0;
        TAILQ_INIT(&headers->list);
}

static void medusa_httprequest_reply_status_uninit (struct medusa_httprequest_reply_status *status)
{
        status->code = 0;
        if (status->value != NULL) {
                free(status->value);
        }
}

static void medusa_httprequest_reply_status_init (struct medusa_httprequest_reply_status *status)
{
        memset(status, 0, sizeof(struct medusa_httprequest_reply_status));
        status->code = 0;
}

static void medusa_httprequest_reply_destroy (struct medusa_httprequest_reply *reply)
{
        if (reply == NULL) {
                return;
        }
        medusa_httprequest_reply_body_uninit(&reply->body);
        medusa_httprequest_reply_headers_uninit(&reply->headers);
        medusa_httprequest_reply_status_uninit(&reply->status);
        free(reply);
}

static struct medusa_httprequest_reply * medusa_httprequest_reply_create (void)
{
        struct medusa_httprequest_reply *reply;
        reply = malloc(sizeof(struct medusa_httprequest_reply));
        if (reply == NULL) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(reply, 0, sizeof(struct medusa_httprequest_reply));
        medusa_httprequest_reply_status_init(&reply->status);
        medusa_httprequest_reply_headers_init(&reply->headers);
        medusa_httprequest_reply_body_init(&reply->body);
        return reply;
}

__attribute__ ((visibility ("default"))) const struct medusa_httprequest_reply * medusa_httprequest_get_reply (const struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return httprequest->reply;
}

__attribute__ ((visibility ("default"))) const struct medusa_httprequest_reply_status * medusa_httprequest_reply_get_status (const struct medusa_httprequest_reply *reply)
{
        if (MEDUSA_IS_ERR_OR_NULL(reply)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return &reply->status;
}

__attribute__ ((visibility ("default"))) int64_t medusa_httprequest_reply_status_get_code (const struct medusa_httprequest_reply_status *status)
{
        if (MEDUSA_IS_ERR_OR_NULL(status)) {
                return -EINVAL;
        }
        return status->code;
}

__attribute__ ((visibility ("default"))) const char * medusa_httprequest_reply_status_get_value (const struct medusa_httprequest_reply_status *status)
{
        if (MEDUSA_IS_ERR_OR_NULL(status)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return status->value;
}

__attribute__ ((visibility ("default"))) const struct medusa_httprequest_reply_headers * medusa_httprequest_reply_get_headers (const struct medusa_httprequest_reply *reply)
{
        if (MEDUSA_IS_ERR_OR_NULL(reply)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return &reply->headers;
}

__attribute__ ((visibility ("default"))) int64_t medusa_httprequest_reply_headers_get_count (const struct medusa_httprequest_reply_headers *headers)
{
        if (MEDUSA_IS_ERR_OR_NULL(headers)) {
                return -EINVAL;
        }
        return headers->count;
}

__attribute__ ((visibility ("default"))) const struct medusa_httprequest_reply_header * medusa_httprequest_reply_headers_get_first (const struct medusa_httprequest_reply_headers *headers)
{
        if (MEDUSA_IS_ERR_OR_NULL(headers)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return TAILQ_FIRST(&headers->list);
}

__attribute__ ((visibility ("default"))) const char * medusa_httprequest_reply_header_get_key (const struct medusa_httprequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return header->key;
}

__attribute__ ((visibility ("default"))) const char * medusa_httprequest_reply_header_get_value (const struct medusa_httprequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return header->value;
}

__attribute__ ((visibility ("default"))) const struct medusa_httprequest_reply_header * medusa_httprequest_reply_header_get_next (const struct medusa_httprequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return TAILQ_NEXT(header, list);
}

__attribute__ ((visibility ("default"))) const struct medusa_httprequest_reply_body * medusa_httprequest_reply_get_body (const struct medusa_httprequest_reply *reply)
{
        if (MEDUSA_IS_ERR_OR_NULL(reply)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return &reply->body;
}

__attribute__ ((visibility ("default"))) int64_t medusa_httprequest_reply_body_get_length (const struct medusa_httprequest_reply_body *body)
{
        if (MEDUSA_IS_ERR_OR_NULL(body)) {
                return -EINVAL;
        }
        return body->length;
}

__attribute__ ((visibility ("default"))) const void * medusa_httprequest_reply_body_get_value (const struct medusa_httprequest_reply_body *body)
{
        if (MEDUSA_IS_ERR_OR_NULL(body)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return body->value;
}

static inline unsigned int httprequest_get_state (const struct medusa_httprequest *httprequest)
{
        return httprequest->state;
}

static inline int httprequest_set_state (struct medusa_httprequest *httprequest, unsigned int state)
{
        if (state == MEDUSA_HTTPREQUEST_STATE_DISCONNECTED) {
                if (!MEDUSA_IS_ERR_OR_NULL(httprequest->tcpsocket)) {
                        medusa_tcpsocket_destroy_unlocked(httprequest->tcpsocket);
                        httprequest->tcpsocket = NULL;
                }
        }
        httprequest->state = state;
        return 0;
}

static int httprequest_httpparser_on_message_begin (http_parser *http_parser)
{
        struct medusa_httprequest *httprequest = http_parser->data;
        if (!MEDUSA_IS_ERR_OR_NULL(httprequest->reply)) {
             medusa_httprequest_reply_destroy(httprequest->reply);
             httprequest->reply = NULL;
        }
        httprequest->reply = medusa_httprequest_reply_create();
        if (MEDUSA_IS_ERR_OR_NULL(httprequest->reply)) {
                return MEDUSA_PTR_ERR(httprequest->reply);
        }
        return 0;
}

static int httprequest_httpparser_on_url (http_parser *http_parser, const char *at, size_t length)
{
        struct medusa_httprequest *httprequest = http_parser->data;
        (void) httprequest;
        (void) at;
        (void) length;
        return 0;
}

static int httprequest_httpparser_on_status (http_parser *http_parser, const char *at, size_t length)
{
        struct medusa_httprequest *httprequest = http_parser->data;
        httprequest->reply->status.code = http_parser->status_code;
        httprequest->reply->status.value = medusa_strndup(at, length);
        if (httprequest->reply->status.value == NULL) {
                return -ENOMEM;
        }
        return 0;
}

static int httprequest_httpparser_on_header_field (http_parser *http_parser, const char *at, size_t length)
{
        int rc;
        struct medusa_httprequest_reply_header *header;
        struct medusa_httprequest *httprequest = http_parser->data;
        header = medusa_httprequest_reply_header_create();
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return MEDUSA_PTR_ERR(header);
        }
        rc = medusa_httprequest_reply_header_set_key(header, at, length);
        if (rc < 0) {
                medusa_httprequest_reply_header_destroy(header);
                return rc;
        }
        TAILQ_INSERT_TAIL(&httprequest->reply->headers.list, header, list);
        httprequest->reply->headers.count += 1;
        return 0;
}

static int httprequest_httpparser_on_header_value (http_parser *http_parser, const char *at, size_t length)
{
        int rc;
        struct medusa_httprequest_reply_header *header;
        struct medusa_httprequest *httprequest = http_parser->data;
        header = TAILQ_LAST(&httprequest->reply->headers.list, medusa_httprequest_reply_headers_list);
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return MEDUSA_PTR_ERR(header);
        }
        rc = medusa_httprequest_reply_header_set_value(header, at, length);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

static int httprequest_httpparser_on_headers_complete (http_parser *http_parser)
{
        int rc;
        struct medusa_httprequest *httprequest = http_parser->data;
        if (httprequest->method != NULL &&
            strcasecmp(httprequest->method, "head") == 0) {
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_RECEIVED);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_RECEIVED, NULL);
                if (rc < 0) {
                        return rc;
                }
        }
        return 0;
}

static int httprequest_httpparser_on_body (http_parser *http_parser, const char *at, size_t length)
{
        void *tmp;
        struct medusa_httprequest *httprequest = http_parser->data;
        tmp = realloc(httprequest->reply->body.value, httprequest->reply->body.length + length + 1);
        if (tmp == NULL) {
                tmp = malloc(httprequest->reply->body.length + length + 1);
                if (tmp == NULL) {
                        return -ENOMEM;
                }
                memcpy(tmp, httprequest->reply->body.value, httprequest->reply->body.length);
                free(httprequest->reply->body.value);
                httprequest->reply->body.value = tmp;
        } else {
                httprequest->reply->body.value = tmp;
        }
        memcpy(httprequest->reply->body.value +  httprequest->reply->body.length, at, length);
        httprequest->reply->body.length += length;
        ((char *) httprequest->reply->body.value)[httprequest->reply->body.length] = '\0';
        return 0;
}

static int httprequest_httpparser_on_message_complete (http_parser *http_parser)
{
        int rc;
        struct medusa_httprequest *httprequest = http_parser->data;
        httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_RECEIVED);
        rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_RECEIVED, NULL);
        if (rc < 0) {
                return rc;
        }
#if 0
        if (!MEDUSA_IS_ERR_OR_NULL(httprequest->tcpsocket)) {
                medusa_tcpsocket_destroy_unlocked(httprequest->tcpsocket);
                httprequest->tcpsocket = NULL;
        }
#endif
        return 0;
}

static int httprequest_httpparser_on_chunk_header (http_parser *http_parser)
{
        struct medusa_httprequest *httprequest = http_parser->data;
        (void) httprequest;
        return 0;
}

static int httprequest_httpparser_on_chunk_complete (http_parser *http_parser)
{
        struct medusa_httprequest *httprequest = http_parser->data;
        (void) httprequest;
        return 0;
}

static int httprequest_tcpsocket_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        struct medusa_monitor *monitor;
        struct medusa_httprequest *httprequest = context;

        (void) param;

        monitor = medusa_tcpsocket_get_monitor(tcpsocket);
        medusa_monitor_lock(monitor);

        if (events & MEDUSA_TCPSOCKET_EVENT_RESOLVING) {
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_RESOLVING);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_RESOLVING, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_RESOLVED) {
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_RESOLVED);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_RESOLVED, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_RESOLVE_TIMEOUT) {
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_RESOLVE_TIMEOUT, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTING) {
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_CONNECTING);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_CONNECTING, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_CONNECTED);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_CONNECTED, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
                http_parser_settings_init(&httprequest->http_parser_settings);
                httprequest->http_parser_settings.on_message_begin      = httprequest_httpparser_on_message_begin;
                httprequest->http_parser_settings.on_url                = httprequest_httpparser_on_url;
                httprequest->http_parser_settings.on_status             = httprequest_httpparser_on_status;
                httprequest->http_parser_settings.on_header_field       = httprequest_httpparser_on_header_field;
                httprequest->http_parser_settings.on_header_value       = httprequest_httpparser_on_header_value;
                httprequest->http_parser_settings.on_headers_complete   = httprequest_httpparser_on_headers_complete;
                httprequest->http_parser_settings.on_body               = httprequest_httpparser_on_body;
                httprequest->http_parser_settings.on_message_complete   = httprequest_httpparser_on_message_complete;
                httprequest->http_parser_settings.on_chunk_header       = httprequest_httpparser_on_chunk_header;
                httprequest->http_parser_settings.on_chunk_complete     = httprequest_httpparser_on_chunk_complete;
                http_parser_init(&httprequest->http_parser, HTTP_RESPONSE);
                httprequest->http_parser.data = httprequest;
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECT_TIMEOUT) {
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_CONNECT_TIMEOUT, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE) {
                if (httprequest_get_state(httprequest) == MEDUSA_HTTPREQUEST_STATE_CONNECTED) {
                        httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_REQUESTING);
                        rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_REQUESTING, NULL);
                        if (rc < 0) {
                                medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED) {
                if (httprequest_get_state(httprequest) == MEDUSA_HTTPREQUEST_STATE_REQUESTING) {
                        httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_REQUESTED);
                        rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_REQUESTED, NULL);
                        if (rc < 0) {
                                medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                if (httprequest_get_state(httprequest) == MEDUSA_HTTPREQUEST_STATE_REQUESTED) {
                        httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_RECEIVING);
                        rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_RECEIVING, NULL);
                        if (rc < 0) {
                                medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }

                while (1) {
                        size_t nparsed;
                        int64_t clength;
                        int64_t niovecs;
                        struct medusa_iovec iovec;

                        niovecs = medusa_buffer_peekv(medusa_tcpsocket_get_read_buffer_unlocked(httprequest->tcpsocket), 0, -1, &iovec, 1);
                        if (niovecs < 0) {
                                medusa_errorf("medusa_buffer_peekv failed, niovecs: %d", (int) niovecs);
                                goto bail;
                        }
                        if (niovecs == 0) {
                                break;
                        }

                        nparsed = http_parser_execute(&httprequest->http_parser, &httprequest->http_parser_settings, iovec.iov_base, iovec.iov_len);
#if 0
                        if (nparsed != iovec.iov_len) {
                                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
                                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED, NULL);
                                if (rc < 0) {
                                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                                        goto bail;
                                }
                                break;
                        }
#endif
                        if (httprequest->http_parser.http_errno != HPE_OK) {
                                struct medusa_httprequest_event_error medusa_httprequest_event_error;
                                medusa_httprequest_event_error.state  = httprequest->state;
                                medusa_httprequest_event_error.error  = EIO;
                                medusa_httprequest_event_error.line   = __LINE__;
                                medusa_httprequest_event_error.reason = MEDUSA_HTTPREQUEST_ERROR_REASON_PARSER;
                                medusa_httprequest_event_error.u.parser.error = httprequest->http_parser.http_errno;
                                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
                                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_ERROR, &medusa_httprequest_event_error);
                                if (rc < 0) {
                                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                                        goto bail;
                                }
                                break;
                        }
                        clength = medusa_buffer_choke(medusa_tcpsocket_get_read_buffer_unlocked(httprequest->tcpsocket), 0, nparsed);
                        if (clength != (int64_t) nparsed) {
                                medusa_errorf("medusa_buffer_choke failed, clength: %d / %d", (int) clength, (int) nparsed);
                                goto bail;
                        }
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ_TIMEOUT) {
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_RECEIVE_TIMEOUT, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_ERROR) {
                struct medusa_tcpsocket_event_error *medusa_tcpsocket_event_error = (struct medusa_tcpsocket_event_error *) param;
                struct medusa_httprequest_event_error medusa_httprequest_event_error;
                medusa_httprequest_event_error.state  = httprequest->state;
                medusa_httprequest_event_error.error  = EIO;
                medusa_httprequest_event_error.line   = __LINE__;
                medusa_httprequest_event_error.reason = MEDUSA_HTTPREQUEST_ERROR_REASON_TCPSOCKET;
                medusa_httprequest_event_error.u.tcpsocket.state = medusa_tcpsocket_event_error->state;
                medusa_httprequest_event_error.u.tcpsocket.error = medusa_tcpsocket_event_error->error;
                medusa_httprequest_event_error.u.tcpsocket.line  = medusa_tcpsocket_event_error->line;
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_ERROR, &medusa_httprequest_event_error);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_DISCONNECTED) {
                if (httprequest_get_state(httprequest) == MEDUSA_HTTPREQUEST_STATE_RECEIVING) {
                        httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_RECEIVED);
                        rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_RECEIVED, NULL);
                        if (rc < 0) {
                                medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }
                httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
                rc = medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_httprequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }

        medusa_monitor_unlock(monitor);
        return 0;
bail:   medusa_monitor_unlock(monitor);
        return -EIO;
}

static int httprequest_init_with_options_unlocked (struct medusa_httprequest *httprequest, const struct medusa_httprequest_init_options *options)
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
        httprequest->dnsresolver     = options->dnsresolver;
        httprequest->resolve_timeout = -1;
        httprequest->connect_timeout = -1;
        httprequest->read_timeout    = -1;
        httprequest->method          = NULL;
        httprequest->headers = medusa_buffer_create(MEDUSA_BUFFER_TYPE_DEFAULT);
        if (MEDUSA_IS_ERR_OR_NULL(httprequest->headers)) {
                return MEDUSA_PTR_ERR(httprequest->headers);
        }
        if (options->resolve_timeout >= 0) {
                rc = medusa_httprequest_set_resolve_timeout_unlocked(httprequest, options->resolve_timeout);
                if (rc < 0) {
                        return rc;
                }
        }
        if (options->connect_timeout >= 0) {
                rc = medusa_httprequest_set_connect_timeout_unlocked(httprequest, options->connect_timeout);
                if (rc < 0) {
                        return rc;
                }
        }
        if (options->read_timeout >= 0) {
                rc = medusa_httprequest_set_read_timeout_unlocked(httprequest, options->read_timeout);
                if (rc < 0) {
                        return rc;
                }
        }
        if (options->method != NULL) {
                rc = medusa_httprequest_set_method_unlocked(httprequest, options->method);
                if (rc < 0) {
                        return rc;
                }
        }
        if (options->url != NULL) {
                rc = medusa_httprequest_set_url_unlocked(httprequest, "%s", options->url);
                if (rc < 0) {
                        return rc;
                }
        }
        rc = medusa_monitor_add_unlocked(options->monitor, &httprequest->subject);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

static void httprequest_uninit_unlocked (struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return;
        }
        if (httprequest->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&httprequest->subject);
        } else {
                medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_init_options_default (struct medusa_httprequest_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_httprequest_init_options));
        options->resolve_timeout = -1;
        options->connect_timeout = -1;
        options->read_timeout    = -1;
        options->method          = "GET";
        options->url             = NULL;
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_httprequest * medusa_httprequest_create_unlocked (struct medusa_monitor *monitor, int (*onevent) (struct medusa_httprequest *httprequest, unsigned int events, void *context, void *param), void *context)
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

__attribute__ ((visibility ("default"))) struct medusa_httprequest * medusa_httprequest_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_httprequest *httprequest, unsigned int events, void *context, void *param), void *context)
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
        rc = httprequest_init_with_options_unlocked(httprequest, options);
        if (rc < 0) {
                medusa_httprequest_destroy_unlocked(httprequest);
                return MEDUSA_ERR_PTR(rc);
        }
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
        httprequest_uninit_unlocked(httprequest);
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
                return MEDUSA_HTTPREQUEST_STATE_UNKNOWN;
        }
        return httprequest_get_state(httprequest);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_httprequest_get_state (const struct medusa_httprequest *httprequest)
{
        unsigned int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_HTTPREQUEST_STATE_UNKNOWN;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_state_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_resolve_timeout_unlocked (struct medusa_httprequest *httprequest, double timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        httprequest->resolve_timeout = timeout;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_resolve_timeout (struct medusa_httprequest *httprequest, double timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_set_resolve_timeout_unlocked(httprequest, timeout);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_httprequest_get_resolve_timeout_unlocked (const struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        return httprequest->resolve_timeout;
}

__attribute__ ((visibility ("default"))) double medusa_httprequest_get_resolve_timeout (const struct medusa_httprequest *httprequest)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_resolve_timeout_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_connect_timeout_unlocked (struct medusa_httprequest *httprequest, double timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        httprequest->connect_timeout = timeout;
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
        return httprequest->connect_timeout;
}

__attribute__ ((visibility ("default"))) double medusa_httprequest_get_connect_timeout (const struct medusa_httprequest *httprequest)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_connect_timeout_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_read_timeout_unlocked (struct medusa_httprequest *httprequest, double timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        httprequest->read_timeout = timeout;
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
        return httprequest->read_timeout;
}

__attribute__ ((visibility ("default"))) double medusa_httprequest_get_read_timeout (const struct medusa_httprequest *httprequest)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_read_timeout_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_method_unlocked (struct medusa_httprequest *httprequest, const char *method)
{
        int i;
        int l;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(method)) {
                return -EINVAL;
        }
        if (httprequest->method != NULL) {
                free(httprequest->method);
        }
        httprequest->method = strdup(method);
        if (httprequest->method == NULL) {
                return -ENOMEM;
        }
        l = strlen(httprequest->method);
        for (i = 0; i < l; i++) {
                httprequest->method[i] = toupper(httprequest->method[i]);
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_method (struct medusa_httprequest *httprequest, const char *method)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(method)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_set_method_unlocked(httprequest, method);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_url_unlocked (struct medusa_httprequest *httprequest, const char *url, ...)
{
        int64_t rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(url)) {
                return -EINVAL;
        }
        va_start(va, url);
        rc = medusa_httprequest_set_vurl_unlocked(httprequest, url, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_url (struct medusa_httprequest *httprequest, const char *url, ...)
{
        int64_t rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(url)) {
                return -EINVAL;
        }
        va_start(va, url);
        rc = medusa_httprequest_set_vurl(httprequest, url, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_vurl_unlocked (struct medusa_httprequest *httprequest, const char *fmt, va_list va)
{
        int rs;
        int rc;
        struct url url;

        int len;
        va_list vp;

        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(fmt)) {
                return -EINVAL;
        }

        rs = -EIO;

        if (httprequest->url != NULL) {
                free(httprequest->url);
                httprequest->url = NULL;
        }

        va_copy(vp, va);
        len = vsnprintf(NULL, 0, fmt, vp);
        va_end(vp);
        if (len < 0) {
                goto bail;
        }
        httprequest->url = malloc(len + 1);
        if (httprequest->url == NULL) {
                rs = -ENOMEM;
                goto bail;
        }
        va_copy(vp, va);
        len = vsnprintf(httprequest->url, len + 1, fmt, vp);
        va_end(vp);
        if (len < 0) {
                rs = -EIO;
                goto bail;
        }

        rc = url_parse(&url, httprequest->url);
        if (rc < 0) {
                rs = -EINVAL;
                goto bail;
        }
        url_uninit(&url);

        return 0;
bail:   if (httprequest->url != NULL) {
                free(httprequest->url);
                httprequest->url = NULL;
        }
        return rs;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_vurl (struct medusa_httprequest *httprequest, const char *url, va_list va)
{
        int64_t rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_set_vurl_unlocked(httprequest, url, va);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_httprequest_get_url_unlocked (const struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return httprequest->url;
}

__attribute__ ((visibility ("default"))) const char * medusa_httprequest_get_url (const struct medusa_httprequest *httprequest)
{
        const char *rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_url_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_header_unlocked (struct medusa_httprequest *httprequest, const char *key, const char *value)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(key)) {
                return -EINVAL;
        }
        rc  = medusa_buffer_printf(httprequest->headers, "%s", key);
        if (rc < 0) {
                return rc;
        }
        if (value != NULL) {
                rc = medusa_buffer_printf(httprequest->headers, ": ");
                if (rc < 0) {
                        return rc;
                }
                rc = medusa_buffer_printf(httprequest->headers, "%s", value);
                if (rc < 0) {
                        return rc;
                }
        }
        rc = medusa_buffer_printf(httprequest->headers, "\r\n");
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_header (struct medusa_httprequest *httprequest, const char *key, const char *value)
{
        int64_t rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_add_header_unlocked(httprequest, key, value);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_headerf_unlocked (struct medusa_httprequest *httprequest, const char *key, const char *value, ...)
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
        rc = medusa_httprequest_add_headerv_unlocked(httprequest, key, value, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_headerf (struct medusa_httprequest *httprequest, const char *key, const char *value, ...)
{
        int64_t rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(key)) {
                return -EINVAL;
        }
        va_start(va, value);
        rc = medusa_httprequest_add_headerv(httprequest, key, value, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_headerv_unlocked (struct medusa_httprequest *httprequest, const char *key, const char *value, va_list va)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(key)) {
                return -EINVAL;
        }
        rc  = medusa_buffer_printf(httprequest->headers, "%s", key);
        if (rc < 0) {
                return rc;
        }
        if (value != NULL) {
                rc = medusa_buffer_printf(httprequest->headers, ": ");
                if (rc < 0) {
                        return rc;
                }
                rc = medusa_buffer_vprintf(httprequest->headers, value, va);
                if (rc < 0) {
                        return rc;
                }
        }
        rc = medusa_buffer_printf(httprequest->headers, "\r\n");
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_headerv (struct medusa_httprequest *httprequest, const char *key, const char *value, va_list va)
{
        int64_t rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_add_headerv_unlocked(httprequest, key, value, va);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_raw_header_unlocked (struct medusa_httprequest *httprequest, const char *value)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(value)) {
                return -EINVAL;
        }
        rc  = medusa_buffer_printf(httprequest->headers, "%s", value);
        if (rc < 0) {
                return rc;
        }
        rc = medusa_buffer_printf(httprequest->headers, "\r\n");
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_raw_header (struct medusa_httprequest *httprequest, const char *value)
{
        int64_t rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_add_raw_header_unlocked(httprequest, value);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_raw_headerf_unlocked (struct medusa_httprequest *httprequest, const char *value, ...)
{
        int64_t rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(value)) {
                return -EINVAL;
        }
        va_start(va, value);
        rc = medusa_httprequest_add_raw_headerv_unlocked(httprequest, value, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_raw_headerf (struct medusa_httprequest *httprequest, const char *value, ...)
{
        int64_t rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(value)) {
                return -EINVAL;
        }
        va_start(va, value);
        rc = medusa_httprequest_add_raw_headerv(httprequest, value, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_raw_headerv_unlocked (struct medusa_httprequest *httprequest, const char *value, va_list va)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(value)) {
                return -EINVAL;
        }
        rc  = medusa_buffer_vprintf(httprequest->headers, value, va);
        if (rc < 0) {
                return rc;
        }
        rc = medusa_buffer_printf(httprequest->headers, "\r\n");
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_add_raw_headerv (struct medusa_httprequest *httprequest, const char *value, va_list va)
{
        int64_t rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_add_raw_headerv_unlocked(httprequest, value, va);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_request_unlocked (struct medusa_httprequest *httprequest, const void *data, int64_t length)
{
        int rc;
        int ret;
        struct url url;
        struct medusa_tcpsocket_connect_options medusa_tcpsocket_connect_options;

        int64_t i;
        int64_t olen;
        int64_t rlen;
        int64_t wlen;
        int64_t niovecs;
        struct medusa_iovec iovecs[16];

        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
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
        if (httprequest->url == NULL) {
                return -EINVAL;
        }

        rc = url_parse(&url, httprequest->url);
        if (rc < 0) {
                return rc;
        }

        ret = 0;

        rc = medusa_tcpsocket_connect_options_default(&medusa_tcpsocket_connect_options);
        if (rc < 0) {
                ret = rc;
                goto bail;
        }
        medusa_tcpsocket_connect_options.monitor         = httprequest->subject.monitor;
        medusa_tcpsocket_connect_options.dnsresolver     = httprequest->dnsresolver;
        medusa_tcpsocket_connect_options.onevent         = httprequest_tcpsocket_onevent;
        medusa_tcpsocket_connect_options.context         = httprequest;
        medusa_tcpsocket_connect_options.protocol        = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
        medusa_tcpsocket_connect_options.address         = url.host;
        medusa_tcpsocket_connect_options.port            = url.port;
        medusa_tcpsocket_connect_options.resolve_timeout = httprequest->resolve_timeout;
        medusa_tcpsocket_connect_options.connect_timeout = httprequest->connect_timeout;
        medusa_tcpsocket_connect_options.read_timeout    = httprequest->read_timeout;
        medusa_tcpsocket_connect_options.nonblocking     = 1;
        medusa_tcpsocket_connect_options.buffered        = 1;
        medusa_tcpsocket_connect_options.enabled         = 1;
        httprequest->tcpsocket = medusa_tcpsocket_connect_with_options_unlocked(&medusa_tcpsocket_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(httprequest->tcpsocket)) {
                ret = MEDUSA_PTR_ERR(httprequest->tcpsocket);
                goto bail;
        }
        rc = medusa_tcpsocket_set_ssl_unlocked(httprequest->tcpsocket, url.ssl);
        if (rc < 0) {
                ret = rc;
                goto bail;
        }

        rc = medusa_tcpsocket_printf_unlocked(httprequest->tcpsocket, "%s /%s HTTP/1.1\r\n", (httprequest->method) ? httprequest->method : "GET", url.path ? url.path : "");
        if (rc < 0) {
                ret = rc;
                goto bail;
        }
        rc = medusa_tcpsocket_printf_unlocked(httprequest->tcpsocket, "Host: %s\r\n", url.host);
        if (rc < 0) {
                ret = rc;
                goto bail;
        }
        olen = 0;
        while (1) {
                niovecs = medusa_buffer_peekv(httprequest->headers, olen, -1, iovecs, sizeof(iovecs) / sizeof(iovecs[0]));
                if (niovecs < 0) {
                        goto bail;
                }
                if (niovecs == 0) {
                        break;
                }
                for (rlen = 0, i = 0; i < niovecs; i++) {
                        rlen += iovecs[i].iov_len;
                }
                wlen = medusa_tcpsocket_writev_unlocked(httprequest->tcpsocket, iovecs, niovecs);
                if (wlen < 0) {
                        goto bail;
                }
                if (wlen != rlen) {
                        goto bail;
                }
                olen += rlen;
        }
        rc = medusa_tcpsocket_printf_unlocked(httprequest->tcpsocket, "Connection: close\r\n");
        if (rc < 0) {
                ret = rc;
                goto bail;
        }
        if (httprequest->method &&  strcasecmp(httprequest->method, "POST") == 0) {
                rc = medusa_tcpsocket_printf_unlocked(httprequest->tcpsocket, "Content-Length: %ld\r\n", (long int) length);
                if (rc < 0) {
                        ret = rc;
                        goto bail;
                }
        }
        rc = medusa_tcpsocket_printf_unlocked(httprequest->tcpsocket, "\r\n");
        if (rc < 0) {
                goto bail;
        }
        if (length > 0) {
                rc = medusa_tcpsocket_write_unlocked(httprequest->tcpsocket, data, length);
                if (rc != length) {
                        goto bail;
                }
        }

        url_uninit(&url);
        return 0;
bail:   url_uninit(&url);
        httprequest_set_state(httprequest, MEDUSA_HTTPREQUEST_STATE_DISCONNECTED);
        medusa_httprequest_onevent_unlocked(httprequest, MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED, NULL);
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_request (struct medusa_httprequest *httprequest, const void *data, int64_t length)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
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
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_make_request_unlocked(httprequest, data, length);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_requestf_unlocked (struct medusa_httprequest *httprequest, const char *data, ...)
{
        int rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        va_start(va, data);
        rc = medusa_httprequest_make_requestv_unlocked(httprequest, data, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_requestf (struct medusa_httprequest *httprequest, const char *data, ...)
{
        int rc;
        va_list va;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        va_start(va, data);
        rc = medusa_httprequest_make_requestv(httprequest, data, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_requestv_unlocked (struct medusa_httprequest *httprequest, const char *data, va_list va)
{
        int rc;
        int length;
        va_list vs;
        char *value;

        value = NULL;

        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }

        if (data != NULL) {
                va_copy(vs, va);
                length = vsnprintf(NULL, 0, data, vs);
                va_end(vs);
                if (length < 0) {
                        return -EIO;
                }
                value = malloc(length + 1);
                if (value == NULL) {
                        return -ENOMEM;
                }
                va_copy(vs, va);
                rc = vsnprintf(value, length + 1, data, vs);
                va_end(vs);
                if (rc < 0) {
                        free(value);
                        return -EIO;
                }
        }

        rc = medusa_httprequest_make_request_unlocked(httprequest, value, (value == NULL) ? 0 : strlen(value));

        if (value != NULL) {
                free(value);
        }
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_requestv (struct medusa_httprequest *httprequest, const char *data, va_list va)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_make_requestv_unlocked(httprequest, data, va);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_get_unlocked (struct medusa_httprequest *httprequest)
{
        return medusa_httprequest_make_request_unlocked(httprequest, NULL, 0);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_get (struct medusa_httprequest *httprequest)
{
        return medusa_httprequest_make_request(httprequest, NULL, 0);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_post_unlocked (struct medusa_httprequest *httprequest, const void *data, int64_t length)
{
        return medusa_httprequest_make_request_unlocked(httprequest, data, length);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_post (struct medusa_httprequest *httprequest, const void *data, int64_t length)
{
        return medusa_httprequest_make_request(httprequest, data, length);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_postf_unlocked (struct medusa_httprequest *httprequest, const char *data, ...)
{
        int rc;
        va_list va;
        va_start(va, data);
        rc = medusa_httprequest_make_requestv_unlocked(httprequest, data, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_postf (struct medusa_httprequest *httprequest, const char *data, ...)
{
        int rc;
        va_list va;
        va_start(va, data);
        rc = medusa_httprequest_make_requestv(httprequest, data, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_postv_unlocked (struct medusa_httprequest *httprequest, const char *data, va_list va)
{
        return medusa_httprequest_make_requestv_unlocked(httprequest, data, va);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_make_postv (struct medusa_httprequest *httprequest, const char *data, va_list va)
{
        return medusa_httprequest_make_requestv(httprequest, data, va);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_onevent_unlocked (struct medusa_httprequest *httprequest, unsigned int events, void *param)
{
        int ret;
        struct medusa_monitor *monitor;
        ret = 0;
        monitor = httprequest->subject.monitor;
        if (httprequest->onevent != NULL) {
                if ((medusa_subject_is_active(&httprequest->subject)) ||
                    (events & MEDUSA_HTTPREQUEST_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        ret = httprequest->onevent(httprequest, events, httprequest->context, param);
                        if (ret < 0) {
                                medusa_errorf("httprequest->onevent failed, ret: %d", ret);
                        }
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_DESTROY) {
                if (httprequest->method != NULL) {
                        free(httprequest->method);
                        httprequest->method = NULL;
                }
                if (httprequest->url != NULL) {
                        free(httprequest->url);
                        httprequest->url = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(httprequest->headers)) {
                        medusa_buffer_destroy(httprequest->headers);
                        httprequest->headers = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(httprequest->tcpsocket)) {
                        medusa_tcpsocket_destroy_unlocked(httprequest->tcpsocket);
                        httprequest->tcpsocket = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(httprequest->reply)) {
                        medusa_httprequest_reply_destroy(httprequest->reply);
                        httprequest->reply = NULL;
                }
#if defined(MEDUSA_HTTPREQUEST_USE_POOL) && (MEDUSA_HTTPREQUEST_USE_POOL == 1)
                medusa_pool_free(httprequest);
#else
                free(httprequest);
#endif
        }
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_onevent (struct medusa_httprequest *httprequest, unsigned int events, void *param)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_onevent_unlocked(httprequest, events, param);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_context_unlocked (struct medusa_httprequest *httprequest, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        httprequest->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_context (struct medusa_httprequest *httprequest, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_set_context_unlocked(httprequest, context);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_httprequest_get_context_unlocked (struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return httprequest->context;
}

__attribute__ ((visibility ("default"))) void * medusa_httprequest_get_context (struct medusa_httprequest *httprequest)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_context_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_userdata_unlocked (struct medusa_httprequest *httprequest, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        httprequest->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_userdata (struct medusa_httprequest *httprequest, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_set_userdata_unlocked(httprequest, userdata);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_httprequest_get_userdata_unlocked (struct medusa_httprequest *httprequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return httprequest->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_httprequest_get_userdata (struct medusa_httprequest *httprequest)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(httprequest->subject.monitor);
        rc = medusa_httprequest_get_userdata_unlocked(httprequest);
        medusa_monitor_unlock(httprequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_userdata_ptr_unlocked (struct medusa_httprequest *httprequest, void *userdata)
{
        return medusa_httprequest_set_userdata_unlocked(httprequest, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_userdata_ptr (struct medusa_httprequest *httprequest, void *userdata)
{
        return medusa_httprequest_set_userdata(httprequest, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_httprequest_get_userdata_ptr_unlocked (struct medusa_httprequest *httprequest)
{
        return medusa_httprequest_get_userdata_unlocked(httprequest);
}

__attribute__ ((visibility ("default"))) void * medusa_httprequest_get_userdata_ptr (struct medusa_httprequest *httprequest)
{
        return medusa_httprequest_get_userdata(httprequest);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_userdata_int_unlocked (struct medusa_httprequest *httprequest, int userdata)
{
        return medusa_httprequest_set_userdata_unlocked(httprequest, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_userdata_int (struct medusa_httprequest *httprequest, int userdata)
{
        return medusa_httprequest_set_userdata(httprequest, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_get_userdata_int_unlocked (struct medusa_httprequest *httprequest)
{
        return (int) (intptr_t) medusa_httprequest_get_userdata_unlocked(httprequest);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_get_userdata_int (struct medusa_httprequest *httprequest)
{
        return (int) (intptr_t) medusa_httprequest_get_userdata(httprequest);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_userdata_uint_unlocked (struct medusa_httprequest *httprequest, unsigned int userdata)
{
        return medusa_httprequest_set_userdata_unlocked(httprequest, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_httprequest_set_userdata_uint (struct medusa_httprequest *httprequest, unsigned int userdata)
{
        return medusa_httprequest_set_userdata(httprequest, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_httprequest_get_userdata_uint_unlocked (struct medusa_httprequest *httprequest)
{
        return (unsigned int) (intptr_t) medusa_httprequest_get_userdata_unlocked(httprequest);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_httprequest_get_userdata_uint (struct medusa_httprequest *httprequest)
{
        return (unsigned int) (uintptr_t) medusa_httprequest_get_userdata(httprequest);
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

__attribute__ ((visibility ("default"))) const char * medusa_httprequest_event_string (unsigned int events)
{
        if (events == MEDUSA_HTTPREQUEST_EVENT_RESOLVING)       return "MEDUSA_HTTPREQUEST_EVENT_RESOLVING";
        if (events == MEDUSA_HTTPREQUEST_EVENT_RESOLVE_TIMEOUT) return "MEDUSA_HTTPREQUEST_EVENT_RESOLVE_TIMEOUT";
        if (events == MEDUSA_HTTPREQUEST_EVENT_RESOLVED)        return "MEDUSA_HTTPREQUEST_EVENT_RESOLVED";
        if (events == MEDUSA_HTTPREQUEST_EVENT_CONNECTING)      return "MEDUSA_HTTPREQUEST_EVENT_CONNECTING";
        if (events == MEDUSA_HTTPREQUEST_EVENT_CONNECT_TIMEOUT) return "MEDUSA_HTTPREQUEST_EVENT_CONNECT_TIMEOUT";
        if (events == MEDUSA_HTTPREQUEST_EVENT_CONNECTED)       return "MEDUSA_HTTPREQUEST_EVENT_CONNECTED";
        if (events == MEDUSA_HTTPREQUEST_EVENT_REQUESTING)      return "MEDUSA_HTTPREQUEST_EVENT_REQUESTING";
        if (events == MEDUSA_HTTPREQUEST_EVENT_REQUEST_TIMEOUT) return "MEDUSA_HTTPREQUEST_EVENT_REQUEST_TIMEOUT";
        if (events == MEDUSA_HTTPREQUEST_EVENT_REQUESTED)       return "MEDUSA_HTTPREQUEST_EVENT_REQUESTED";
        if (events == MEDUSA_HTTPREQUEST_EVENT_RECEIVING)       return "MEDUSA_HTTPREQUEST_EVENT_RECEIVING";
        if (events == MEDUSA_HTTPREQUEST_EVENT_RECEIVE_TIMEOUT) return "MEDUSA_HTTPREQUEST_EVENT_RECEIVE_TIMEOUT";
        if (events == MEDUSA_HTTPREQUEST_EVENT_RECEIVED)        return "MEDUSA_HTTPREQUEST_EVENT_RECEIVED";
        if (events == MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED)    return "MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED";
        if (events == MEDUSA_HTTPREQUEST_EVENT_ERROR)           return "MEDUSA_HTTPREQUEST_EVENT_ERROR";
        if (events == MEDUSA_HTTPREQUEST_EVENT_DESTROY)         return "MEDUSA_HTTPREQUEST_EVENT_DESTROY";
        return "MEDUSA_HTTPREQUEST_EVENT_UNKNOWN";
}

__attribute__ ((visibility ("default"))) const char * medusa_httprequest_state_string (unsigned int state)
{
        if (state == MEDUSA_HTTPREQUEST_STATE_UNKNOWN)          return "MEDUSA_HTTPREQUEST_STATE_UNKNOWN";
        if (state == MEDUSA_HTTPREQUEST_STATE_DISCONNECTED)     return "MEDUSA_HTTPREQUEST_STATE_DISCONNECTED";
        if (state == MEDUSA_HTTPREQUEST_STATE_RESOLVING)        return "MEDUSA_HTTPREQUEST_STATE_RESOLVING";
        if (state == MEDUSA_HTTPREQUEST_STATE_RESOLVED)         return "MEDUSA_HTTPREQUEST_STATE_RESOLVED";
        if (state == MEDUSA_HTTPREQUEST_STATE_CONNECTING)       return "MEDUSA_HTTPREQUEST_STATE_CONNECTING";
        if (state == MEDUSA_HTTPREQUEST_STATE_CONNECTED)        return "MEDUSA_HTTPREQUEST_STATE_CONNECTED";
        if (state == MEDUSA_HTTPREQUEST_STATE_REQUESTING)       return "MEDUSA_HTTPREQUEST_STATE_REQUESTING";
        if (state == MEDUSA_HTTPREQUEST_STATE_REQUESTED)        return "MEDUSA_HTTPREQUEST_STATE_REQUESTED";
        if (state == MEDUSA_HTTPREQUEST_STATE_RECEIVING)        return "MEDUSA_HTTPREQUEST_STATE_RECEIVING";
        if (state == MEDUSA_HTTPREQUEST_STATE_RECEIVED)         return "MEDUSA_HTTPREQUEST_STATE_RECEIVED";
        return "MEDUSA_HTTPREQUEST_STATE_UNKNOWN";
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
