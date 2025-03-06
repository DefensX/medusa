
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "../3rdparty/http-parser/http_parser.h"

#include "strndup.h"
#include "error.h"
#include "pool.h"
#include "base64.h"
#include "sha1.h"
#include "queue.h"
#include "subject-struct.h"
#include "iovec.h"
#include "buffer.h"
#include "tcpsocket.h"
#include "tcpsocket-private.h"
#include "websocketclient.h"
#include "websocketclient-private.h"
#include "websocketclient-struct.h"
#include "monitor-private.h"

#if defined(__GNUC__) && __GNUC__ >= 7
        #define FALL_THROUGH __attribute__ ((fallthrough))
#else
        #define FALL_THROUGH ((void)0)
#endif /* __GNUC__ >= 7 */

#define MEDUSA_WEBSOCKETCLIENT_USE_POOL         1

#if defined(MEDUSA_WEBSOCKETCLIENT_USE_POOL) && (MEDUSA_WEBSOCKETCLIENT_USE_POOL == 1)
static struct medusa_pool *g_pool_websocketclient;
#endif

#define WS_FRAGMENT_FIN                 0x80

#define WS_NONBLOCK                     0x02

#define WS_OPCODE_CONTINUE              0x00
#define WS_OPCODE_TEXT                  0x01
#define WS_OPCODE_BINARY                0x02
#define WS_OPCODE_CLOSE                 0x08
#define WS_OPCODE_PING                  0x09
#define WS_OPCODE_PONG                  0x0a

#define WS_CLOSE_NORMAL                 1000
#define WS_CLOSE_GOING_AWAY             1001
#define WS_CLOSE_PROTOCOL_ERROR         1002
#define WS_CLOSE_NOT_ALLOWED            1003
#define WS_CLOSE_RESERVED               1004
#define WS_CLOSE_NO_CODE                1005
#define WS_CLOSE_DIRTY                  1006
#define WS_CLOSE_WRONG_TYPE             1007
#define WS_CLOSE_POLICY_VIOLATION       1008
#define WS_CLOSE_MESSAGE_TOO_BIG        1009
#define WS_CLOSE_UNEXPECTED_ERROR       1011

enum {
        MEDUSA_WEBSOCKETCLIENT_FLAG_NONE         = (1 <<  0),
        MEDUSA_WEBSOCKETCLIENT_FLAG_ENABLED      = (1 <<  1)
#define MEDUSA_WEBSOCKETCLIENT_FLAG_NONE         MEDUSA_WEBSOCKETCLIENT_FLAG_NONE
#define MEDUSA_WEBSOCKETCLIENT_FLAG_ENABLED      MEDUSA_WEBSOCKETCLIENT_FLAG_ENABLED
};

enum {
        MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_START         = 0,
        MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_HEADER        = 1,
        MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_PAYLOAD       = 2,
        MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_FINISH        = 3
#define MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_START         MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_START
#define MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_HEADER        MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_HEADER
#define MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_PAYLOAD       MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_PAYLOAD
#define MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_FINISH        MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_FINISH
};

static inline void websocketclient_set_flag (struct medusa_websocketclient *websocketclient, unsigned int flag)
{
        websocketclient->flags = flag;
}

static inline void websocketclient_add_flag (struct medusa_websocketclient *websocketclient, unsigned int flag)
{
        websocketclient->flags |= flag;
}

static inline void websocketclient_del_flag (struct medusa_websocketclient *websocketclient, unsigned int flag)
{
        websocketclient->flags &= ~flag;
}

static inline int websocketclient_has_flag (const struct medusa_websocketclient *websocketclient, unsigned int flag)
{
        return !!(websocketclient->flags & flag);
}

static inline int websocketclient_set_state (struct medusa_websocketclient *websocketclient, unsigned int state)
{
        int rc;
        unsigned int pstate;
        struct medusa_websocketclient_event_state_changed medusa_websocketclient_event_state_changed;
        websocketclient->error = 0;
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_ERROR) {
                if (!MEDUSA_IS_ERR_OR_NULL(websocketclient->tcpsocket)) {
                        medusa_tcpsocket_destroy_unlocked(websocketclient->tcpsocket);
                        websocketclient->tcpsocket = NULL;
                }
        }
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED) {
                if (!MEDUSA_IS_ERR_OR_NULL(websocketclient->tcpsocket)) {
                        medusa_tcpsocket_destroy_unlocked(websocketclient->tcpsocket);
                        websocketclient->tcpsocket = NULL;
                }
        }
        pstate = websocketclient->state;
        websocketclient->state = state;
        medusa_websocketclient_event_state_changed.pstate = pstate;
        medusa_websocketclient_event_state_changed.state  = websocketclient->state;
        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_STATE_CHANGED, &medusa_websocketclient_event_state_changed);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

static int websocketclient_httpparser_on_message_begin (http_parser *http_parser)
{
        struct medusa_websocketclient *websocketclient = http_parser->data;
        (void) websocketclient;
        return 0;
}

static int websocketclient_httpparser_on_url (http_parser *http_parser, const char *at, size_t length)
{
        struct medusa_websocketclient *websocketclient = http_parser->data;
        (void) websocketclient;
        (void) at;
        (void) length;
        return 0;
}

static int websocketclient_httpparser_on_status (http_parser *http_parser, const char *at, size_t length)
{
        struct medusa_websocketclient *websocketclient = http_parser->data;
        (void) websocketclient;
        (void) at;
        (void) length;
        return 0;
}

static int websocketclient_httpparser_on_header_field (http_parser *http_parser, const char *at, size_t length)
{
        int rc;
        struct medusa_websocketclient_event_response_header websocketclient_event_response_header;
        struct medusa_websocketclient *websocketclient = http_parser->data;

        if (websocketclient->http_parser_header_field != NULL &&
            websocketclient->http_parser_header_value != NULL) {
                websocketclient_event_response_header.field = websocketclient->http_parser_header_field;
                websocketclient_event_response_header.value = websocketclient->http_parser_header_value;
                rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_HEADER, &websocketclient_event_response_header);
                if (rc < 0) {
                        return rc;
                }
                if (strcasecmp(websocketclient->http_parser_header_field, "Sec-WebSocket-Accept") == 0) {
                        if (websocketclient->sec_websocket_accept != NULL) {
                                free(websocketclient->sec_websocket_accept);
                        }
                        websocketclient->sec_websocket_accept = strdup(websocketclient->http_parser_header_value);
                        if (websocketclient->sec_websocket_accept == NULL) {
                                return -ENOMEM;
                        }
                }
                if (strcasecmp(websocketclient->http_parser_header_field, "Sec-WebSocket-Protocol") == 0) {
                        if (websocketclient->sec_websocket_protocol != NULL) {
                                free(websocketclient->sec_websocket_protocol);
                        }
                        websocketclient->sec_websocket_protocol = strdup(websocketclient->http_parser_header_value);
                        if (websocketclient->sec_websocket_protocol == NULL) {
                                return -ENOMEM;
                        }
                }
                if (websocketclient->http_parser_header_field != NULL) {
                        free(websocketclient->http_parser_header_field);
                        websocketclient->http_parser_header_field = NULL;
                }
        }

        if (websocketclient->http_parser_header_value != NULL) {
                free(websocketclient->http_parser_header_value);
                websocketclient->http_parser_header_value = NULL;
        }

        if (websocketclient->http_parser_header_field != NULL) {
                char *tmp = realloc(websocketclient->http_parser_header_field, strlen(websocketclient->http_parser_header_field) + length + 1);
                if (tmp == NULL) {
                        return -ENOMEM;
                }
                websocketclient->http_parser_header_field = tmp;
                strncat(websocketclient->http_parser_header_field, at, length);
        } else {
                websocketclient->http_parser_header_field = medusa_strndup(at, length);
                if (websocketclient->http_parser_header_field == NULL) {
                        return -ENOMEM;
                }
        }

        return 0;
}

static int websocketclient_httpparser_on_header_value (http_parser *http_parser, const char *at, size_t length)
{
        struct medusa_websocketclient *websocketclient = http_parser->data;

        if (websocketclient->http_parser_header_value != NULL) {
                char *tmp = realloc(websocketclient->http_parser_header_value, strlen(websocketclient->http_parser_header_value) + length + 1);
                if (tmp == NULL) {
                        return -ENOMEM;
                }
                websocketclient->http_parser_header_value = tmp;
                strncat(websocketclient->http_parser_header_value, at, length);
        } else {
                websocketclient->http_parser_header_value = medusa_strndup(at, length);
                if (websocketclient->http_parser_header_value == NULL) {
                        return -ENOMEM;
                }
        }

        return 0;
}

static int websocketclient_httpparser_on_headers_complete (http_parser *http_parser)
{
        int rc;
        struct medusa_websocketclient_event_response_header websocketclient_event_response_header;
        struct medusa_websocketclient *websocketclient = http_parser->data;

        if (websocketclient->http_parser_header_field != NULL &&
            websocketclient->http_parser_header_value != NULL) {
                websocketclient_event_response_header.field = websocketclient->http_parser_header_field;
                websocketclient_event_response_header.value = websocketclient->http_parser_header_value;
                rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_HEADER, &websocketclient_event_response_header);
                if (rc < 0) {
                        return rc;
                }
                if (strcasecmp(websocketclient->http_parser_header_field, "Sec-WebSocket-Accept") == 0) {
                        if (websocketclient->sec_websocket_accept != NULL) {
                                free(websocketclient->sec_websocket_accept);
                        }
                        websocketclient->sec_websocket_accept = strdup(websocketclient->http_parser_header_value);
                        if (websocketclient->sec_websocket_accept == NULL) {
                                return -ENOMEM;
                        }
                }
                if (strcasecmp(websocketclient->http_parser_header_field, "Sec-WebSocket-Protocol") == 0) {
                        if (websocketclient->sec_websocket_protocol != NULL) {
                                free(websocketclient->sec_websocket_protocol);
                        }
                        websocketclient->sec_websocket_protocol = strdup(websocketclient->http_parser_header_value);
                        if (websocketclient->sec_websocket_protocol == NULL) {
                                return -ENOMEM;
                        }
                }
        }
        if (websocketclient->http_parser_header_field != NULL) {
                free(websocketclient->http_parser_header_field);
                websocketclient->http_parser_header_field = NULL;
        }
        if (websocketclient->http_parser_header_value != NULL) {
                free(websocketclient->http_parser_header_value);
                websocketclient->http_parser_header_value = NULL;
        }
        return 0;
}

static int websocketclient_httpparser_on_body (http_parser *http_parser, const char *at, size_t length)
{
        struct medusa_websocketclient *websocketclient = http_parser->data;
        (void) websocketclient;
        (void) at;
        (void) length;
        return 0;
}

static int websocketclient_httpparser_on_message_complete (http_parser *http_parser)
{
        int rc;
        struct medusa_websocketclient *websocketclient = http_parser->data;
        (void) websocketclient;
        rc = websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_RESPONSE_RECEIVED);
        if (rc < 0) {
                return rc;
        }
        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_RECEIVED, NULL);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

static int websocketclient_httpparser_on_chunk_header (http_parser *http_parser)
{
        struct medusa_websocketclient *websocketclient = http_parser->data;
        (void) websocketclient;
        return 0;
}

static int websocketclient_httpparser_on_chunk_complete (http_parser *http_parser)
{
        struct medusa_websocketclient *websocketclient = http_parser->data;
        (void) websocketclient;
        return 0;
}

static int websocketclient_tcpsocket_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        int error;
        struct medusa_monitor *monitor;
        struct medusa_websocketclient *websocketclient = (struct medusa_websocketclient *) context;

        if (events & MEDUSA_TCPSOCKET_EVENT_DESTROY) {
                return 0;
        }

        monitor = medusa_tcpsocket_get_monitor(tcpsocket);
        medusa_monitor_lock(monitor);

        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                int i;
                int l;
                char key_nonce[16];

                websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_SENDING_REQUEST);
                rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_SENDING_REQUEST, NULL);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }

                srand(time(NULL));
	        for (i = 0; i < 16; i++) {
		        key_nonce[i] = rand() & 0xff;
	        }
                l = medusa_base64_encode_length(16);
                websocketclient->sec_websocket_key = malloc(l);
                if (websocketclient->sec_websocket_key == NULL) {
                        error = -ENOMEM;
                        goto bail;
                }
                medusa_base64_encode(websocketclient->sec_websocket_key, key_nonce, 16);

                rc = medusa_tcpsocket_printf_unlocked(websocketclient->tcpsocket,
                        "GET %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
			"Upgrade: websocket\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "Sec-WebSocket-Protocol: %s\r\n"
                        "Sec-WebSocket-Key: %s\r\n"
                        "Connection: keep-alive, upgrade\r\n"
                        "Pragma: no-cache\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Upgrade: websocket\r\n"
                        "\r\n",
                        (websocketclient->sec_websocket_path) ? websocketclient->sec_websocket_path : "/",
                        websocketclient->host,
                        (websocketclient->sec_websocket_protocol) ? websocketclient->sec_websocket_protocol : "generic",
			websocketclient->sec_websocket_key);
                if (rc < 0) {
                        websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_ERROR);
                        websocketclient->error = rc;
                        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR, NULL);
                        if (rc < 0) {
                                error = EIO;
                                goto bail;
                        }
                        goto out;
                }
        } else if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                if (websocketclient->state == MEDUSA_WEBSOCKETCLIENT_STATE_REQUEST_SENT) {
                        rc = websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_RECEIVING_RESPONSE);
                        if (rc < 0) {
                                error = rc;
                                goto bail;
                        }
                        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_RECEIVING_RESPONSE, NULL);
                        if (rc < 0) {
                                error = rc;
                                goto bail;
                        }
                }
                if (websocketclient->state == MEDUSA_WEBSOCKETCLIENT_STATE_RECEIVING_RESPONSE) {
                        int64_t siovecs;
                        int64_t niovecs;
                        int64_t iiovecs;
                        struct medusa_iovec iovecs[1];

                        size_t nparsed;
                        size_t tparsed;
                        int64_t clength;

                        siovecs = sizeof(iovecs) / sizeof(iovecs[0]);
                        niovecs = medusa_buffer_peekv(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), 0, -1, iovecs, siovecs);
                        if (niovecs < 0) {
                                error = niovecs;
                                goto bail;
                        }

                        tparsed = 0;
                        for (iiovecs = 0; iiovecs < niovecs; iiovecs++) {
                                nparsed = http_parser_execute(&websocketclient->http_parser, &websocketclient->http_parser_settings, iovecs[iiovecs].iov_base, iovecs[iiovecs].iov_len);
                                if (websocketclient->http_parser.http_errno != 0) {
                                        error = -EIO;
                                        goto bail;
                                }
                                tparsed += nparsed;
                                if (nparsed != iovecs[iiovecs].iov_len) {
                                        break;
                                }
                        }
                        clength = medusa_buffer_choke(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), 0, tparsed);
                        if (clength != (int64_t) tparsed) {
                                error = -EIO;
                                goto bail;
                        }
                }
                if (websocketclient->state == MEDUSA_WEBSOCKETCLIENT_STATE_RESPONSE_RECEIVED) {
                        char *str;
                        char hash[MEDUSA_SHA1_LENGTH];
                        char *base64;
                        const char *gid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                        const char *key = websocketclient->sec_websocket_key;

                        if (key == NULL) {
                                error = -EIO;
                                goto bail;
                        }

                        str = malloc(strlen(key) + strlen(gid) + 1);
                        if (str == NULL) {
                                error = -ENOMEM;
                                goto bail;
                        }
                        memset(str, 0, strlen(key) + strlen(gid) + 1);
                        strcat(str, key);
                        strcat(str, gid);
                        medusa_sha1(hash, str, strlen(str));
                        base64 = malloc(medusa_base64_encode_length(MEDUSA_SHA1_LENGTH));
                        if (base64 == NULL) {
                                free(str);
                                error = -ENOMEM;
                                goto bail;
                        }
                        medusa_base64_encode(base64, hash, MEDUSA_SHA1_LENGTH);

                        if (websocketclient->sec_websocket_accept == NULL ||
                            strcasecmp(base64, websocketclient->sec_websocket_accept) != 0) {
                                websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_ERROR);
                                websocketclient->error = EPERM;
                                rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR, NULL);
                                if (rc < 0) {
                                        error = EIO;
                                        goto bail;
                                }
                                free(base64);
                                free(str);
                                goto out;
                        }
                        free(base64);
                        free(str);

                        rc = websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED);
                        if (rc < 0) {
                                error = rc;
                                goto bail;
                        }
                        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_CONNECTED, NULL);
                        if (rc < 0) {
                                error = rc;
                                goto bail;
                        }

                        free(websocketclient->sec_websocket_key);
                        websocketclient->sec_websocket_key = NULL;

                        free(websocketclient->sec_websocket_accept);
                        websocketclient->sec_websocket_accept = NULL;

                        free(websocketclient->sec_websocket_path);
                        websocketclient->sec_websocket_path = NULL;

                        free(websocketclient->sec_websocket_protocol);
                        websocketclient->sec_websocket_protocol = NULL;
                }
                if (websocketclient->state == MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED) {
restart_buffer:
                        switch (websocketclient->frame_state) {
                                case MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_START: {
                                        int64_t rlength;
                                        rlength = medusa_buffer_get_length(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket));
                                        if (rlength < 0) {
                                                error = rlength;
                                                goto bail;
                                        }
                                        if (rlength < 2) {
                                                goto short_buffer;
                                        }
                                        websocketclient->frame_state          = MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_HEADER;
                                        websocketclient->frame_mask_offset    = 0;
                                        websocketclient->frame_payload_offset = 0;
                                        websocketclient->frame_payload_length = 0;
                                        FALL_THROUGH;
                                }
                                case MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_HEADER: {
                                        uint8_t uint8;
                                        rc = medusa_buffer_peek_uint8(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), 1, &uint8);
                                        if (rc < 0) {
                                                error = rc;
                                                goto bail;
                                        }
                                        switch (uint8 & 0x7f) {
                                                case 126: {
                                                        uint16_t uint16;
                                                        int64_t rlength;
                                                        rlength = medusa_buffer_get_length(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket));
                                                        if (rlength < 0) {
                                                                error = rlength;
                                                                goto bail;
                                                        }
                                                        if (rlength < 4) {
                                                                goto short_buffer;
                                                        }
                                                        rc = medusa_buffer_peek_uint16_be(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), 2, &uint16);
                                                        if (rc < 0) {
                                                                error = rc;
                                                                goto bail;
                                                        }
                                                        websocketclient->frame_mask_offset    = 4;
                                                        websocketclient->frame_payload_offset = websocketclient->frame_mask_offset + 4;
                                                        websocketclient->frame_payload_length = uint16;
                                                        break;
                                                }
                                                case 127: {
                                                        uint64_t uint64;
                                                        int64_t rlength;
                                                        rlength = medusa_buffer_get_length(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket));
                                                        if (rlength < 0) {
                                                                error = rlength;
                                                                goto bail;
                                                        }
                                                        if (rlength < 10) {
                                                                goto short_buffer;
                                                        }
                                                        rc = medusa_buffer_peek_uint64_be(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), 2, &uint64);
                                                        if (rc < 0) {
                                                                error = rc;
                                                                goto bail;
                                                        }
                                                        websocketclient->frame_mask_offset    = 10;
                                                        websocketclient->frame_payload_offset = websocketclient->frame_mask_offset + 4;
                                                        websocketclient->frame_payload_length = uint64;
                                                        break;
                                                }
                                                default:
                                                        websocketclient->frame_mask_offset    = 2;
                                                        websocketclient->frame_payload_offset = websocketclient->frame_mask_offset + 4;
                                                        websocketclient->frame_payload_length = uint8 & 0x7f;
                                                        break;
                                        }
                                        if ((uint8 & 0x80) == 0) {
                                                websocketclient->frame_mask_offset     = 0;
                                                websocketclient->frame_payload_offset -= 4;
                                        }
                                        websocketclient->frame_state = MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_PAYLOAD;
                                        FALL_THROUGH;
                                }
                                case MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_PAYLOAD: {
                                        unsigned int i;
                                        int64_t rlength;
                                        uint8_t *payload;

                                        rlength = medusa_buffer_get_length(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket));
                                        if (rlength < 0) {
                                                error = rlength;
                                                goto bail;
                                        }
                                        if (rlength < websocketclient->frame_payload_offset + websocketclient->frame_payload_length) {
                                                goto short_buffer;
                                        }

                                        payload = medusa_buffer_linearize(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), websocketclient->frame_payload_offset, websocketclient->frame_payload_length);
                                        if (MEDUSA_IS_ERR_OR_NULL(payload)) {
                                                error = MEDUSA_PTR_ERR(payload);
                                                goto bail;
                                        }
                                        if (websocketclient->frame_mask_offset != 0) {
                                                uint8_t mask[4];
                                                rc  = medusa_buffer_peek_uint8(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), websocketclient->frame_mask_offset + 0, &mask[0]);
                                                rc |= medusa_buffer_peek_uint8(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), websocketclient->frame_mask_offset + 1, &mask[1]);
                                                rc |= medusa_buffer_peek_uint8(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), websocketclient->frame_mask_offset + 2, &mask[2]);
                                                rc |= medusa_buffer_peek_uint8(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), websocketclient->frame_mask_offset + 3, &mask[3]);
                                                if (rc < 0) {
                                                        error = rc;
                                                        goto bail;
                                                }
                                                for (i = 0; i < websocketclient->frame_payload_length; i++) {
                                                        payload[i] = payload[i] ^ mask[i & 3];
                                                }
                                        }

                                        websocketclient->frame_state = MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_FINISH;
                                        FALL_THROUGH;
                                }
                                case MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_FINISH: {
                                        uint8_t uint8;
                                        uint8_t opcode;
                                        int64_t clength;
                                        uint8_t *payload;
                                        struct medusa_websocketclient_event_message medusa_websocketclient_event_message;

                                        rc = medusa_buffer_peek_uint8(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), 0, &uint8);
                                        if (rc < 0) {
                                                error = rc;
                                                goto bail;
                                        }

                                        payload = medusa_buffer_linearize(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), websocketclient->frame_payload_offset, websocketclient->frame_payload_length);
                                        if (MEDUSA_IS_ERR_OR_NULL(payload)) {
                                                error = MEDUSA_PTR_ERR(payload);
                                                goto bail;
                                        }

                                        opcode = uint8 & 0x0f;
                                        medusa_websocketclient_event_message.final   = !!(uint8 & 0x80);
                                        medusa_websocketclient_event_message.type    = (opcode == WS_OPCODE_CLOSE) ? MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CLOSE :
                                                                                              (opcode == WS_OPCODE_PING) ? MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PING :
                                                                                              (opcode == WS_OPCODE_PONG) ? MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PONG :
                                                                                              (opcode == WS_OPCODE_TEXT) ? MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT :
                                                                                              (opcode == WS_OPCODE_BINARY) ? MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_BINARY :
                                                                                              MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CONTINUATION;
                                        medusa_websocketclient_event_message.length  = websocketclient->frame_payload_length;
                                        medusa_websocketclient_event_message.payload = payload;
                                        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_MESSAGE, &medusa_websocketclient_event_message);
                                        if (rc < 0) {
                                                error = rc;
                                                goto bail;
                                        }
                                        clength = medusa_buffer_choke(medusa_tcpsocket_get_read_buffer_unlocked(tcpsocket), 0, websocketclient->frame_payload_offset + websocketclient->frame_payload_length);
                                        if (clength != websocketclient->frame_payload_offset + websocketclient->frame_payload_length) {
                                                error = -EIO;
                                                goto bail;
                                        }

                                        if (opcode == WS_OPCODE_CLOSE) {
                                                rc = websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED);
                                                if (rc < 0) {
                                                        error = rc;
                                                        goto bail;
                                                }
                                                rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_DISCONNECTED, NULL);
                                                if (rc < 0) {
                                                        error = rc;
                                                        goto bail;
                                                }
                                                medusa_websocketclient_destroy_unlocked(websocketclient);
                                                goto out;
                                        }

                                        websocketclient->frame_state = MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_START;
                                        goto restart_buffer;
                                }
                        }
short_buffer:
                        ;

                }
        } else if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE) {
                if (websocketclient->state == MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED) {
                        struct medusa_tcpsocket_event_buffered_write *medusa_tcpsocket_event_buffered_write = (struct medusa_tcpsocket_event_buffered_write *) param;
                        struct medusa_websocketclient_event_buffered_write medusa_websocketclient_event_buffered_write;
                        medusa_websocketclient_event_buffered_write.length    = medusa_tcpsocket_event_buffered_write->length;
                        medusa_websocketclient_event_buffered_write.remaining = medusa_tcpsocket_event_buffered_write->remaining;
                        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE, &medusa_websocketclient_event_buffered_write);
                        if (rc < 0) {
                                error = rc;
                                goto bail;
                        }
                }
        } else if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED) {
                if (websocketclient->state == MEDUSA_WEBSOCKETCLIENT_STATE_SENDING_REQUEST) {
                        websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_REQUEST_SENT);
                        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_REQUEST_SENT, NULL);
                        if (rc < 0) {
                                error = rc;
                                goto bail;
                        }
                        http_parser_settings_init(&websocketclient->http_parser_settings);
                        websocketclient->http_parser_settings.on_message_begin      = websocketclient_httpparser_on_message_begin;
                        websocketclient->http_parser_settings.on_url                = websocketclient_httpparser_on_url;
                        websocketclient->http_parser_settings.on_status             = websocketclient_httpparser_on_status;
                        websocketclient->http_parser_settings.on_header_field       = websocketclient_httpparser_on_header_field;
                        websocketclient->http_parser_settings.on_header_value       = websocketclient_httpparser_on_header_value;
                        websocketclient->http_parser_settings.on_headers_complete   = websocketclient_httpparser_on_headers_complete;
                        websocketclient->http_parser_settings.on_body               = websocketclient_httpparser_on_body;
                        websocketclient->http_parser_settings.on_message_complete   = websocketclient_httpparser_on_message_complete;
                        websocketclient->http_parser_settings.on_chunk_header       = websocketclient_httpparser_on_chunk_header;
                        websocketclient->http_parser_settings.on_chunk_complete     = websocketclient_httpparser_on_chunk_complete;
                        http_parser_init(&websocketclient->http_parser, HTTP_RESPONSE);
                        websocketclient->http_parser.data = websocketclient;
                } else if (websocketclient->state == MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED) {
                        rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE_FINISHED, NULL);
                        if (rc < 0) {
                                error = rc;
                                goto bail;
                        }
                }
        } else if (events & MEDUSA_TCPSOCKET_EVENT_ERROR) {
                struct medusa_tcpsocket_event_error *medusa_tcpsocket_event_error = (struct medusa_tcpsocket_event_error *) param;
                rc = websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_ERROR);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
                websocketclient->error = medusa_tcpsocket_event_error->error;
                rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR, NULL);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
                medusa_websocketclient_destroy_unlocked(websocketclient);
        } else if (events & MEDUSA_TCPSOCKET_EVENT_DISCONNECTED) {
                rc = websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
                rc = medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_DISCONNECTED, NULL);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
                medusa_websocketclient_destroy_unlocked(websocketclient);
        } else if (events & MEDUSA_TCPSOCKET_EVENT_STATE_CHANGED) {
        }

out:    medusa_monitor_unlock(monitor);
        return 0;
bail:   websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_ERROR);
        websocketclient->error = -error;
        medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR, NULL);
        medusa_monitor_unlock(monitor);
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_connect_options_default (struct medusa_websocketclient_connect_options *options)
{
        if (options == NULL) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_websocketclient_connect_options));
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_websocketclient * medusa_websocketclient_connect_unlocked (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_websocketclient *websocketclient, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_websocketclient_connect_options options;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        rc = medusa_websocketclient_connect_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor         = monitor;
        options.protocol        = protocol;
        options.address         = address;
        options.port            = port;
        options.server_path     = "/";
        options.server_protocol = "websocket";
        options.enabled         = 1;
        options.onevent         = onevent;
        options.context         = context;
        return medusa_websocketclient_connect_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_websocketclient * medusa_websocketclient_connect (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_websocketclient *websocketclient, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_websocketclient *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_websocketclient_connect_unlocked(monitor, protocol, address, port, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_websocketclient * medusa_websocketclient_connect_with_options_unlocked (const struct medusa_websocketclient_connect_options *options)
{
        int rc;
        int error;

        struct medusa_tcpsocket *connected;
        struct medusa_tcpsocket_connect_options medusa_tcpsocket_connect_options;

        struct medusa_websocketclient *websocketclient;

        websocketclient = NULL;

        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->address)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }

#if defined(MEDUSA_WEBSOCKETCLIENT_USE_POOL) && (MEDUSA_WEBSOCKETCLIENT_USE_POOL == 1)
        websocketclient = medusa_pool_malloc(g_pool_websocketclient);
#else
        websocketclient = malloc(sizeof(struct medusa_websocketclient));
#endif
        if (websocketclient == NULL) {
                error = -ENOMEM;
                goto bail;
        }
        memset(websocketclient, 0, sizeof(struct medusa_websocketclient));
        medusa_subject_set_type(&websocketclient->subject, MEDUSA_SUBJECT_TYPE_WEBSOCKETCLIENT);
        websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED);
        websocketclient_set_flag(websocketclient, MEDUSA_WEBSOCKETCLIENT_FLAG_NONE);
        if (options->server_path != NULL) {
                websocketclient->sec_websocket_path = strdup(options->server_path);
                if (websocketclient->sec_websocket_path == NULL) {
                        error = -ENOMEM;
                        goto bail;
                }
        }
        if (options->server_protocol != NULL) {
                websocketclient->sec_websocket_protocol = strdup(options->server_protocol);
                if (websocketclient->sec_websocket_protocol == NULL) {
                        error = -ENOMEM;
                        goto bail;
                }
        }
        if (options->address != NULL) {
                websocketclient->host = strdup(options->address);
                if (websocketclient->host == NULL) {
                        error = -ENOMEM;
                        goto bail;
                }
        }
        websocketclient->onevent = options->onevent;
        websocketclient->context = options->context;
        websocketclient->frame_state = MEDUSA_WEBSOCKETCLIENT_FRAME_STATE_START;
        rc = medusa_monitor_add_unlocked(options->monitor, &websocketclient->subject);
        if (rc < 0) {
                error = rc;
                goto bail;
        }

        rc = medusa_websocketclient_set_enabled_unlocked(websocketclient, options->enabled);
        if (rc < 0) {
                error = rc;
                goto bail;
        }

        rc = medusa_tcpsocket_connect_options_default(&medusa_tcpsocket_connect_options);
        if (rc < 0) {
                error = rc;
                goto bail;
        }
        medusa_tcpsocket_connect_options.monitor                = options->monitor;
        medusa_tcpsocket_connect_options.port                   = options->port;
        medusa_tcpsocket_connect_options.protocol               = (options->protocol == MEDUSA_WEBSOCKETCLIENT_PROTOCOL_IPV4) ? MEDUSA_TCPSOCKET_PROTOCOL_IPV4 :
                                                                  (options->protocol == MEDUSA_WEBSOCKETCLIENT_PROTOCOL_IPV6) ? MEDUSA_TCPSOCKET_PROTOCOL_IPV6 :
                                                                  MEDUSA_TCPSOCKET_PROTOCOL_ANY;
        medusa_tcpsocket_connect_options.address                = options->address;
        medusa_tcpsocket_connect_options.buffered               = 1;
        medusa_tcpsocket_connect_options.nodelay                = 1;
        medusa_tcpsocket_connect_options.nonblocking            = 1;
        medusa_tcpsocket_connect_options.ssl                    = options->ssl;
        medusa_tcpsocket_connect_options.ssl_ca_certificate     = options->ssl_ca_certificate;
        medusa_tcpsocket_connect_options.ssl_privatekey         = options->ssl_privatekey;
        medusa_tcpsocket_connect_options.ssl_verify             = options->ssl_verify;
        medusa_tcpsocket_connect_options.enabled                = options->enabled;
        medusa_tcpsocket_connect_options.onevent                = websocketclient_tcpsocket_onevent;
        medusa_tcpsocket_connect_options.context                = websocketclient;
        connected = medusa_tcpsocket_connect_with_options_unlocked(&medusa_tcpsocket_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(connected)) {
                error = MEDUSA_PTR_ERR(connected);
                goto bail;
        }
        websocketclient->tcpsocket = connected;

        return websocketclient;
bail:   if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(error);
        }
        websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_ERROR);
        websocketclient->error = -error;
        medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR, NULL);
        return websocketclient;
}

__attribute__ ((visibility ("default"))) struct medusa_websocketclient * medusa_websocketclient_connect_with_options (const struct medusa_websocketclient_connect_options *options)
{
        struct medusa_websocketclient *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_websocketclient_connect_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_websocketclient_destroy_unlocked (struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return;
        }
        if (websocketclient->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&websocketclient->subject);
        } else {
                medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) void medusa_websocketclient_destroy (struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        medusa_websocketclient_destroy_unlocked(websocketclient);
        medusa_monitor_unlock(websocketclient->subject.monitor);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_websocketclient_get_state_unlocked (const struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_WEBSOCKETCLIENT_STATE_UNKNOWN;
        }
        return websocketclient->state;
}

__attribute__ ((visibility ("default"))) unsigned int medusa_websocketclient_get_state (const struct medusa_websocketclient *websocketclient)
{
        unsigned int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_WEBSOCKETCLIENT_STATE_UNKNOWN;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_get_state_unlocked(websocketclient);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_enabled_unlocked (struct medusa_websocketclient *websocketclient, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        if (websocketclient_has_flag(websocketclient, MEDUSA_WEBSOCKETCLIENT_FLAG_ENABLED) == !!enabled) {
                return 0;
        }
        if (enabled) {
                websocketclient_add_flag(websocketclient, MEDUSA_WEBSOCKETCLIENT_FLAG_ENABLED);
        } else {
                websocketclient_del_flag(websocketclient, MEDUSA_WEBSOCKETCLIENT_FLAG_ENABLED);
        }
        if (!MEDUSA_IS_ERR_OR_NULL(websocketclient->tcpsocket)) {
                rc = medusa_tcpsocket_set_enabled_unlocked(websocketclient->tcpsocket, enabled);
                if (rc < 0) {
                        return rc;
                }
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_enabled (struct medusa_websocketclient *websocketclient, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_set_enabled_unlocked(websocketclient, enabled);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_get_enabled_unlocked (const struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        return websocketclient_has_flag(websocketclient, MEDUSA_WEBSOCKETCLIENT_FLAG_ENABLED);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_get_enabled (const struct medusa_websocketclient *websocketclient)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_get_enabled_unlocked(websocketclient);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_buffer * medusa_websocketclient_get_read_buffer_unlocked (const struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return medusa_tcpsocket_get_read_buffer_unlocked(websocketclient->tcpsocket);
}

__attribute__ ((visibility ("default"))) struct medusa_buffer * medusa_websocketclient_get_read_buffer (const struct medusa_websocketclient *websocketclient)
{
        struct medusa_buffer *buffer;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        buffer = medusa_websocketclient_get_read_buffer_unlocked(websocketclient);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return buffer;
}

__attribute__ ((visibility ("default"))) struct medusa_buffer * medusa_websocketclient_get_write_buffer_unlocked (const struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return medusa_tcpsocket_get_write_buffer_unlocked(websocketclient->tcpsocket);
}

__attribute__ ((visibility ("default"))) struct medusa_buffer * medusa_websocketclient_get_write_buffer (const struct medusa_websocketclient *websocketclient)
{
        struct medusa_buffer *buffer;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        buffer = medusa_websocketclient_get_write_buffer_unlocked(websocketclient);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return buffer;
}

__attribute__ ((visibility ("default"))) int64_t medusa_websocketclient_write_unlocked (struct medusa_websocketclient *websocketclient, unsigned int final, unsigned int type, const void *data, int64_t length)
{
        int rc;
        int error;
        uint8_t uint8;

        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }

        uint8  = 0;
        uint8 |= (final) ? 0x80 : 0x00;
        uint8 |= (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CONTINUATION) ? 0x00 :
                 (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CLOSE)        ? 0x08 :
                 (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PING)         ? 0x09 :
                 (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PONG)         ? 0x0a :
                 (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT)         ? 0x01 :
                 (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_BINARY)       ? 0x02 :
                 0x00;
        rc = medusa_buffer_append_uint8_be(medusa_tcpsocket_get_write_buffer_unlocked(websocketclient->tcpsocket), uint8);
        if (rc < 0) {
                error = rc;
                goto bail;
        }

        if (length <= 125) {
                uint8  = 0;
                uint8 |= length;
                rc = medusa_buffer_append_uint8_be(medusa_tcpsocket_get_write_buffer_unlocked(websocketclient->tcpsocket), uint8);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
        } else if (length <= 0xffff) {
                uint8  = 0;
                uint8 |= 126;
                rc = medusa_buffer_append_uint8_be(medusa_tcpsocket_get_write_buffer_unlocked(websocketclient->tcpsocket), uint8);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
                rc = medusa_buffer_append_uint16_be(medusa_tcpsocket_get_write_buffer_unlocked(websocketclient->tcpsocket), length);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
        } else {
                uint8  = 0;
                uint8 |= 127;
                rc = medusa_buffer_append_uint8_be(medusa_tcpsocket_get_write_buffer_unlocked(websocketclient->tcpsocket), uint8);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
                rc = medusa_buffer_append_uint64_be(medusa_tcpsocket_get_write_buffer_unlocked(websocketclient->tcpsocket), length);
                if (rc < 0) {
                        error = rc;
                        goto bail;
                }
        }
        rc = medusa_buffer_append(medusa_tcpsocket_get_write_buffer_unlocked(websocketclient->tcpsocket), data, length);
        if (rc < 0) {
                error = rc;
                goto bail;
        }

        return length;
bail:   websocketclient_set_state(websocketclient, MEDUSA_WEBSOCKETCLIENT_STATE_ERROR);
        websocketclient->error = -error;
        medusa_websocketclient_onevent_unlocked(websocketclient, MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR, NULL);
        return error;
}

__attribute__ ((visibility ("default"))) int64_t medusa_websocketclient_write (struct medusa_websocketclient *websocketclient, unsigned int final, unsigned int type, const void *data, int64_t length)
{
        int64_t rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_write_unlocked(websocketclient, final, type, data, length);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_get_sockname_unlocked (struct medusa_websocketclient *websocketclient, struct sockaddr_storage *sockaddr)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        if (sockaddr == NULL) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient->tcpsocket)) {
                return -EINVAL;
        }
        rc = medusa_tcpsocket_get_sockname_unlocked(websocketclient->tcpsocket, sockaddr);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_get_sockname (struct medusa_websocketclient *websocketclient, struct sockaddr_storage *sockaddr)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_get_sockname_unlocked(websocketclient, sockaddr);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_get_peername_unlocked (struct medusa_websocketclient *websocketclient, struct sockaddr_storage *sockaddr)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        if (sockaddr == NULL) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient->tcpsocket)) {
                return -EINVAL;
        }
        rc = medusa_tcpsocket_get_peername_unlocked(websocketclient->tcpsocket, sockaddr);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_get_peername (struct medusa_websocketclient *websocketclient, struct sockaddr_storage *sockaddr)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_get_peername_unlocked(websocketclient, sockaddr);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_context_unlocked (struct medusa_websocketclient *websocketclient, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        websocketclient->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_context (struct medusa_websocketclient *websocketclient, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_set_context_unlocked(websocketclient, context);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_websocketclient_get_context_unlocked (struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return websocketclient->context;
}

__attribute__ ((visibility ("default"))) void * medusa_websocketclient_get_context (struct medusa_websocketclient *websocketclient)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_get_context_unlocked(websocketclient);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_userdata_unlocked (struct medusa_websocketclient *websocketclient, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        websocketclient->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_userdata (struct medusa_websocketclient *websocketclient, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_set_userdata_unlocked(websocketclient, userdata);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_websocketclient_get_userdata_unlocked (struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return websocketclient->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_websocketclient_get_userdata (struct medusa_websocketclient *websocketclient)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_get_userdata_unlocked(websocketclient);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_userdata_ptr_unlocked (struct medusa_websocketclient *websocketclient, void *userdata)
{
        return medusa_websocketclient_set_userdata_unlocked(websocketclient, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_userdata_ptr (struct medusa_websocketclient *websocketclient, void *userdata)
{
        return medusa_websocketclient_set_userdata(websocketclient, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_websocketclient_get_userdata_ptr_unlocked (struct medusa_websocketclient *websocketclient)
{
        return medusa_websocketclient_get_userdata_unlocked(websocketclient);
}

__attribute__ ((visibility ("default"))) void * medusa_websocketclient_get_userdata_ptr (struct medusa_websocketclient *websocketclient)
{
        return medusa_websocketclient_get_userdata(websocketclient);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_userdata_int_unlocked (struct medusa_websocketclient *websocketclient, int userdata)
{
        return medusa_websocketclient_set_userdata_unlocked(websocketclient, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_userdata_int (struct medusa_websocketclient *websocketclient, int userdata)
{
        return medusa_websocketclient_set_userdata(websocketclient, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_get_userdata_int_unlocked (struct medusa_websocketclient *websocketclient)
{
        return (int) (intptr_t) medusa_websocketclient_get_userdata_unlocked(websocketclient);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_get_userdata_int (struct medusa_websocketclient *websocketclient)
{
        return (int) (intptr_t) medusa_websocketclient_get_userdata(websocketclient);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_userdata_uint_unlocked (struct medusa_websocketclient *websocketclient, unsigned int userdata)
{
        return medusa_websocketclient_set_userdata_unlocked(websocketclient, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_set_userdata_uint (struct medusa_websocketclient *websocketclient, unsigned int userdata)
{
        return medusa_websocketclient_set_userdata(websocketclient, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_websocketclient_get_userdata_uint_unlocked (struct medusa_websocketclient *websocketclient)
{
        return (unsigned int) (intptr_t) medusa_websocketclient_get_userdata_unlocked(websocketclient);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_websocketclient_get_userdata_uint (struct medusa_websocketclient *websocketclient)
{
        return (unsigned int) (uintptr_t) medusa_websocketclient_get_userdata(websocketclient);
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_onevent_unlocked (struct medusa_websocketclient *websocketclient, unsigned int events, void *param)
{
        int ret;
        struct medusa_monitor *monitor;
        ret = 0;
        monitor = websocketclient->subject.monitor;
        if (websocketclient->onevent != NULL) {
                if ((medusa_subject_is_active(&websocketclient->subject)) ||
                    (events & MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        ret = websocketclient->onevent(websocketclient, events, websocketclient->context, param);
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY) {
                if (websocketclient->sec_websocket_path != NULL) {
                        free(websocketclient->sec_websocket_path);
                        websocketclient->sec_websocket_path = NULL;
                }
                if (websocketclient->sec_websocket_protocol != NULL) {
                        free(websocketclient->sec_websocket_protocol);
                        websocketclient->sec_websocket_protocol = NULL;
                }
                if (websocketclient->sec_websocket_key != NULL) {
                        free(websocketclient->sec_websocket_key);
                        websocketclient->sec_websocket_key = NULL;
                }
                if (websocketclient->sec_websocket_accept != NULL) {
                        free(websocketclient->sec_websocket_accept);
                        websocketclient->sec_websocket_accept = NULL;
                }
                if (websocketclient->http_parser_header_field != NULL) {
                        free(websocketclient->http_parser_header_field);
                        websocketclient->http_parser_header_field = NULL;
                }
                if (websocketclient->http_parser_header_value != NULL) {
                        free(websocketclient->http_parser_header_value);
                        websocketclient->http_parser_header_value = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(websocketclient->tcpsocket)) {
                        medusa_tcpsocket_destroy_unlocked(websocketclient->tcpsocket);
                        websocketclient->tcpsocket = NULL;
                }
                if (websocketclient->host != NULL) {
                        free(websocketclient->host);
                        websocketclient->host = NULL;
                }
#if defined(MEDUSA_WEBSOCKETCLIENT_USE_POOL) && (MEDUSA_WEBSOCKETCLIENT_USE_POOL == 1)
                medusa_pool_free(websocketclient);
#else
                free(websocketclient);
#endif
        }
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_websocketclient_onevent (struct medusa_websocketclient *websocketclient, unsigned int events, void *param)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return -EINVAL;
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_onevent_unlocked(websocketclient, events, param);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_websocketclient_get_monitor_unlocked (struct medusa_websocketclient *websocketclient)
{
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return websocketclient->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_websocketclient_get_monitor (struct medusa_websocketclient *websocketclient)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(websocketclient->subject.monitor);
        rc = medusa_websocketclient_get_monitor_unlocked(websocketclient);
        medusa_monitor_unlock(websocketclient->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_websocketclient_event_string (unsigned int events)
{
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR)                       return "MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_SENDING_REQUEST)             return "MEDUSA_WEBSOCKETCLIENT_EVENT_SENDING_REQUEST";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_REQUEST_SENT)                return "MEDUSA_WEBSOCKETCLIENT_EVENT_REQUEST_SENT";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_RECEIVING_RESPONSE)          return "MEDUSA_WEBSOCKETCLIENT_EVENT_RECEIVING_RESPONSE";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_HEADER)             return "MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_HEADER";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_RECEIVED)           return "MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_RECEIVED";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_CONNECTED)                   return "MEDUSA_WEBSOCKETCLIENT_EVENT_CONNECTED";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_MESSAGE)                     return "MEDUSA_WEBSOCKETCLIENT_EVENT_MESSAGE";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE)              return "MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE_FINISHED)     return "MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE_FINISHED";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_DISCONNECTED)                return "MEDUSA_WEBSOCKETCLIENT_EVENT_DISCONNECTED";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_STATE_CHANGED)               return "MEDUSA_WEBSOCKETCLIENT_EVENT_STATE_CHANGED";
        if (events == MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY)                     return "MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY";
        return "MEDUSA_WEBSOCKETCLIENT_EVENT_UNKNOWN";
}

__attribute__ ((visibility ("default"))) const char * medusa_websocketclient_state_string (unsigned int state)
{
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_UNKNOWN)              return "MEDUSA_WEBSOCKETCLIENT_STATE_UNKNOWN";
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED)         return "MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED";
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_SENDING_REQUEST)      return "MEDUSA_WEBSOCKETCLIENT_STATE_SENDING_REQUEST";
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_REQUEST_SENT)         return "MEDUSA_WEBSOCKETCLIENT_STATE_REQUEST_SENT";
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_RECEIVING_RESPONSE)   return "MEDUSA_WEBSOCKETCLIENT_STATE_RECEIVING_RESPONSE";
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_RESPONSE_RECEIVED)    return "MEDUSA_WEBSOCKETCLIENT_STATE_RESPONSE_RECEIVED";
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED)            return "MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED";
        if (state == MEDUSA_WEBSOCKETCLIENT_STATE_ERROR)                return "MEDUSA_WEBSOCKETCLIENT_STATE_ERROR";
        return "MEDUSA_WEBSOCKETCLIENT_STATE_UNKNOWN";
}

__attribute__ ((visibility ("default"))) const char * medusa_websocketclient_frame_type_string (unsigned int type)
{
        if (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CONTINUATION)     return "MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CONTINUATION";
        if (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CLOSE)            return "MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CLOSE";
        if (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PING)             return "MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PING";
        if (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PONG)             return "MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PONG";
        if (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT)             return "MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT";
        if (type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_BINARY)           return "MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_BINARY";
        return "MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_UNKNOWN";
}

__attribute__ ((constructor)) static void websocketclient_constructor (void)
{
#if defined(MEDUSA_WEBSOCKETCLIENT_USE_POOL) && (MEDUSA_WEBSOCKETCLIENT_USE_POOL == 1)
        g_pool_websocketclient = medusa_pool_create("medusa-websocketclient", sizeof(struct medusa_websocketclient), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void websocketclient_destructor (void)
{
#if defined(MEDUSA_WEBSOCKETCLIENT_USE_POOL) && (MEDUSA_WEBSOCKETCLIENT_USE_POOL == 1)
        if (g_pool_websocketclient != NULL) {
                medusa_pool_destroy(g_pool_websocketclient);
        }
#endif
}
