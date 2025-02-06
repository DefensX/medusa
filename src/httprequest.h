
#if !defined(MEDUSA_HTTPREQUEST_H)
#define MEDUSA_HTTPREQUEST_H

struct medusa_monitor;
struct medusa_httprequest;
struct medusa_httprequest_reply;
struct medusa_httprequest_reply_header;
struct medusa_httprequest_reply_headers;
struct medusa_httprequest_reply_body;

enum {
        MEDUSA_HTTPREQUEST_PROTOCOL_ANY                = 0,
        MEDUSA_HTTPREQUEST_PROTOCOL_IPV4               = 1,
        MEDUSA_HTTPREQUEST_PROTOCOL_IPV6               = 2
#define MEDUSA_HTTPREQUEST_PROTOCOL_ANY                MEDUSA_HTTPREQUEST_PROTOCOL_ANY
#define MEDUSA_HTTPREQUEST_PROTOCOL_IPV4               MEDUSA_HTTPREQUEST_PROTOCOL_IPV4
#define MEDUSA_HTTPREQUEST_PROTOCOL_IPV6               MEDUSA_HTTPREQUEST_PROTOCOL_IPV6
};

enum {
        MEDUSA_HTTPREQUEST_EVENT_RESOLVING              = (1 <<  0), /* 0x00000001 */
        MEDUSA_HTTPREQUEST_EVENT_RESOLVE_TIMEOUT        = (1 <<  1), /* 0x00000002 */
        MEDUSA_HTTPREQUEST_EVENT_RESOLVED               = (1 <<  2), /* 0x00000004 */
        MEDUSA_HTTPREQUEST_EVENT_CONNECTING             = (1 <<  3), /* 0x00000008 */
        MEDUSA_HTTPREQUEST_EVENT_CONNECT_TIMEOUT        = (1 <<  4), /* 0x00000010 */
        MEDUSA_HTTPREQUEST_EVENT_CONNECTED              = (1 <<  5), /* 0x00000020 */
        MEDUSA_HTTPREQUEST_EVENT_REQUESTING             = (1 <<  6), /* 0x00000040 */
        MEDUSA_HTTPREQUEST_EVENT_REQUEST_TIMEOUT        = (1 <<  7), /* 0x00000080 */
        MEDUSA_HTTPREQUEST_EVENT_REQUESTED              = (1 <<  8), /* 0x00000100 */
        MEDUSA_HTTPREQUEST_EVENT_RECEIVING              = (1 <<  9), /* 0x00000200 */
        MEDUSA_HTTPREQUEST_EVENT_RECEIVE_TIMEOUT        = (1 << 10), /* 0x00000400 */
        MEDUSA_HTTPREQUEST_EVENT_RECEIVED               = (1 << 11), /* 0x00000800 */
        MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED           = (1 << 12), /* 0x00001000 */
        MEDUSA_HTTPREQUEST_EVENT_ERROR                  = (1 << 13), /* 0x00002000 */
        MEDUSA_HTTPREQUEST_EVENT_DESTROY                = (1 << 14), /* 0x00004000 */
#define MEDUSA_HTTPREQUEST_EVENT_RESOLVING              MEDUSA_HTTPREQUEST_EVENT_RESOLVING
#define MEDUSA_HTTPREQUEST_EVENT_RESOLVE_TIMEOUT        MEDUSA_HTTPREQUEST_EVENT_RESOLVE_TIMEOUT
#define MEDUSA_HTTPREQUEST_EVENT_RESOLVED               MEDUSA_HTTPREQUEST_EVENT_RESOLVED
#define MEDUSA_HTTPREQUEST_EVENT_CONNECTING             MEDUSA_HTTPREQUEST_EVENT_CONNECTING
#define MEDUSA_HTTPREQUEST_EVENT_CONNECT_TIMEOUT        MEDUSA_HTTPREQUEST_EVENT_CONNECT_TIMEOUT
#define MEDUSA_HTTPREQUEST_EVENT_CONNECTED              MEDUSA_HTTPREQUEST_EVENT_CONNECTED
#define MEDUSA_HTTPREQUEST_EVENT_REQUESTING             MEDUSA_HTTPREQUEST_EVENT_REQUESTING
#define MEDUSA_HTTPREQUEST_EVENT_REQUEST_TIMEOUT        MEDUSA_HTTPREQUEST_EVENT_REQUEST_TIMEOUT
#define MEDUSA_HTTPREQUEST_EVENT_REQUESTED              MEDUSA_HTTPREQUEST_EVENT_REQUESTED
#define MEDUSA_HTTPREQUEST_EVENT_RECEIVING              MEDUSA_HTTPREQUEST_EVENT_RECEIVING
#define MEDUSA_HTTPREQUEST_EVENT_RECEIVE_TIMEOUT        MEDUSA_HTTPREQUEST_EVENT_RECEIVE_TIMEOUT
#define MEDUSA_HTTPREQUEST_EVENT_RECEIVED               MEDUSA_HTTPREQUEST_EVENT_RECEIVED
#define MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED           MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED
#define MEDUSA_HTTPREQUEST_EVENT_ERROR                  MEDUSA_HTTPREQUEST_EVENT_ERROR
#define MEDUSA_HTTPREQUEST_EVENT_DESTROY                MEDUSA_HTTPREQUEST_EVENT_DESTROY
};

enum {
        MEDUSA_HTTPREQUEST_STATE_UNKNOWN                = 0,
        MEDUSA_HTTPREQUEST_STATE_DISCONNECTED           = 1,
        MEDUSA_HTTPREQUEST_STATE_RESOLVING              = 2,
        MEDUSA_HTTPREQUEST_STATE_RESOLVED               = 3,
        MEDUSA_HTTPREQUEST_STATE_CONNECTING             = 4,
        MEDUSA_HTTPREQUEST_STATE_CONNECTED              = 5,
        MEDUSA_HTTPREQUEST_STATE_REQUESTING             = 6,
        MEDUSA_HTTPREQUEST_STATE_REQUESTED              = 7,
        MEDUSA_HTTPREQUEST_STATE_RECEIVING              = 8,
        MEDUSA_HTTPREQUEST_STATE_RECEIVED               = 9,
#define MEDUSA_HTTPREQUEST_STATE_UNKNOWN                MEDUSA_HTTPREQUEST_STATE_UNKNOWN
#define MEDUSA_HTTPREQUEST_STATE_DISCONNECTED           MEDUSA_HTTPREQUEST_STATE_DISCONNECTED
#define MEDUSA_HTTPREQUEST_STATE_RESOLVING              MEDUSA_HTTPREQUEST_STATE_RESOLVING
#define MEDUSA_HTTPREQUEST_STATE_RESOLVED               MEDUSA_HTTPREQUEST_STATE_RESOLVED
#define MEDUSA_HTTPREQUEST_STATE_CONNECTING             MEDUSA_HTTPREQUEST_STATE_CONNECTING
#define MEDUSA_HTTPREQUEST_STATE_CONNECTED              MEDUSA_HTTPREQUEST_STATE_CONNECTED
#define MEDUSA_HTTPREQUEST_STATE_REQUESTING             MEDUSA_HTTPREQUEST_STATE_REQUESTING
#define MEDUSA_HTTPREQUEST_STATE_REQUESTED              MEDUSA_HTTPREQUEST_STATE_REQUESTED
#define MEDUSA_HTTPREQUEST_STATE_RECEIVING              MEDUSA_HTTPREQUEST_STATE_RECEIVING
#define MEDUSA_HTTPREQUEST_STATE_RECEIVED               MEDUSA_HTTPREQUEST_STATE_RECEIVED
};

struct medusa_httprequest_init_options {
        struct medusa_monitor *monitor;
        struct medusa_dnsresolver *dnsresolver;
        double resolve_timeout;
        double connect_timeout;
        double read_timeout;
        const char *method;
        const char *url;
        int (*onevent) (struct medusa_httprequest *httprequest, unsigned int events, void *context, void *param);
        void *context;
};

enum {
        MEDUSA_HTTPREQUEST_ERROR_REASON_PARSER          = 0,
        MEDUSA_HTTPREQUEST_ERROR_REASON_TCPSOCKET       = 1
#define MEDUSA_HTTPREQUEST_ERROR_REASON_PARSER          MEDUSA_HTTPREQUEST_ERROR_REASON_PARSER
#define MEDUSA_HTTPREQUEST_ERROR_REASON_TCPSOCKET       MEDUSA_HTTPREQUEST_ERROR_REASON_TCPSOCKET
};

struct medusa_httprequest_event_error {
        unsigned int state;
        unsigned int error;
        unsigned int line;
        unsigned int reason;
        union {
                struct {
                        unsigned int state;
                        unsigned int error;
                        unsigned int line;
                } tcpsocket;
                struct {
                        unsigned int error;
                } parser;
        } u;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_httprequest_init_options_default (struct medusa_httprequest_init_options *options);

struct medusa_httprequest * medusa_httprequest_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_httprequest *httprequest, unsigned int events, void *context, void *param), void *context);
struct medusa_httprequest * medusa_httprequest_create_with_options (const struct medusa_httprequest_init_options *options);
void medusa_httprequest_destroy (struct medusa_httprequest *httprequest);

unsigned int medusa_httprequest_get_state (const struct medusa_httprequest *httprequest);

int medusa_httprequest_set_resolve_timeout (struct medusa_httprequest *httprequest, double timeout);
double medusa_httprequest_get_resolve_timeout (const struct medusa_httprequest *httprequest);

int medusa_httprequest_set_connect_timeout (struct medusa_httprequest *httprequest, double timeout);
double medusa_httprequest_get_connect_timeout (const struct medusa_httprequest *httprequest);

int medusa_httprequest_set_read_timeout (struct medusa_httprequest *httprequest, double timeout);
double medusa_httprequest_get_read_timeout (const struct medusa_httprequest *httprequest);

int medusa_httprequest_set_method (struct medusa_httprequest *httprequest, const char *method);

int medusa_httprequest_set_url (struct medusa_httprequest *httprequest, const char *url, ...) __attribute__((format(printf, 2, 3)));
int medusa_httprequest_set_vurl (struct medusa_httprequest *httprequest, const char *url, va_list va);
const char * medusa_httprequest_get_url (const struct medusa_httprequest *httprequest);

int medusa_httprequest_add_header (struct medusa_httprequest *httprequest, const char *key, const char *value);
int medusa_httprequest_add_headerf (struct medusa_httprequest *httprequest, const char *key, const char *value, ...) __attribute__((format(printf, 3, 4)));
int medusa_httprequest_add_headerv (struct medusa_httprequest *httprequest, const char *key, const char *value, va_list va);

int medusa_httprequest_add_raw_header (struct medusa_httprequest *httprequest, const char *value);
int medusa_httprequest_add_raw_headerf (struct medusa_httprequest *httprequest, const char *value, ...) __attribute__((format(printf, 2, 3)));
int medusa_httprequest_add_raw_headerv (struct medusa_httprequest *httprequest, const char *value, va_list va);

int medusa_httprequest_make_request (struct medusa_httprequest *httprequest, const void *data, int64_t length);
int medusa_httprequest_make_requestf (struct medusa_httprequest *httprequest, const char *data, ...) __attribute__((format(printf, 2, 3)));
int medusa_httprequest_make_requestv (struct medusa_httprequest *httprequest, const char *data, va_list va);

int medusa_httprequest_make_get (struct medusa_httprequest *httprequest);

int medusa_httprequest_make_post (struct medusa_httprequest *httprequest, const void *data, int64_t length);
int medusa_httprequest_make_postf (struct medusa_httprequest *httprequest, const char *data, ...) __attribute__((format(printf, 2, 3)));
int medusa_httprequest_make_postv (struct medusa_httprequest *httprequest, const char *data, va_list va);

int medusa_httprequest_onevent (struct medusa_httprequest *httprequest, unsigned int events, void *param);

int medusa_httprequest_set_context (struct medusa_httprequest *httprequest, void *context);
void * medusa_httprequest_get_context (struct medusa_httprequest *httprequest);

int medusa_httprequest_set_userdata (struct medusa_httprequest *httprequest, void *userdata);
void * medusa_httprequest_get_userdata (struct medusa_httprequest *httprequest);

int medusa_httprequest_set_userdata_ptr (struct medusa_httprequest *httprequest, void *userdata);
void * medusa_httprequest_get_userdata_ptr (struct medusa_httprequest *httprequest);

int medusa_httprequest_set_userdata_int (struct medusa_httprequest *httprequest, int userdara);
int medusa_httprequest_get_userdata_int (struct medusa_httprequest *httprequest);

int medusa_httprequest_set_userdata_uint (struct medusa_httprequest *httprequest, unsigned int userdata);
unsigned int medusa_httprequest_get_userdata_uint (struct medusa_httprequest *httprequest);

struct medusa_monitor * medusa_httprequest_get_monitor (struct medusa_httprequest *httprequest);

const struct medusa_httprequest_reply * medusa_httprequest_get_reply (const struct medusa_httprequest *httprequest);

const struct medusa_httprequest_reply_status * medusa_httprequest_reply_get_status (const struct medusa_httprequest_reply *reply);
int64_t medusa_httprequest_reply_status_get_code (const struct medusa_httprequest_reply_status *status);
const char * medusa_httprequest_reply_status_get_value (const struct medusa_httprequest_reply_status *status);

const struct medusa_httprequest_reply_headers * medusa_httprequest_reply_get_headers (const struct medusa_httprequest_reply *reply);
int64_t medusa_httprequest_reply_headers_get_count (const struct medusa_httprequest_reply_headers *headers);
const struct medusa_httprequest_reply_header * medusa_httprequest_reply_headers_get_first (const struct medusa_httprequest_reply_headers *headers);

const char * medusa_httprequest_reply_header_get_key (const struct medusa_httprequest_reply_header *header);
const char * medusa_httprequest_reply_header_get_value (const struct medusa_httprequest_reply_header *header);
const struct medusa_httprequest_reply_header * medusa_httprequest_reply_header_get_next (const struct medusa_httprequest_reply_header *header);

const struct medusa_httprequest_reply_body * medusa_httprequest_reply_get_body (const struct medusa_httprequest_reply *reply);
int64_t medusa_httprequest_reply_body_get_length (const struct medusa_httprequest_reply_body *body);
const void * medusa_httprequest_reply_body_get_value (const struct medusa_httprequest_reply_body *body);

const char * medusa_httprequest_event_string (unsigned int events);
const char * medusa_httprequest_state_string (unsigned int state);

#ifdef __cplusplus
}
#endif

#endif
