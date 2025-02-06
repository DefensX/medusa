
#if !defined(MEDUSA_HTTPSERVER_H)
#define MEDUSA_HTTPSERVER_H

struct sockaddr_storage;

struct medusa_monitor;
struct medusa_httpserver;
struct medusa_httpserver_client;
struct medusa_httpserver_client_request;
struct medusa_httpserver_client_request_option;
struct medusa_httpserver_client_request_options;
struct medusa_httpserver_client_request_header;
struct medusa_httpserver_client_request_headers;
struct medusa_httpserver_client_request_body;

enum {
        MEDUSA_HTTPSERVER_PROTOCOL_ANY                     = 0,
        MEDUSA_HTTPSERVER_PROTOCOL_IPV4                    = 1,
        MEDUSA_HTTPSERVER_PROTOCOL_IPV6                    = 2
#define MEDUSA_HTTPSERVER_PROTOCOL_ANY                     MEDUSA_HTTPSERVER_PROTOCOL_ANY
#define MEDUSA_HTTPSERVER_PROTOCOL_IPV4                    MEDUSA_HTTPSERVER_PROTOCOL_IPV4
#define MEDUSA_HTTPSERVER_PROTOCOL_IPV6                    MEDUSA_HTTPSERVER_PROTOCOL_IPV6
};

enum {
        MEDUSA_HTTPSERVER_EVENT_STARTED                    = (1 << 0),
        MEDUSA_HTTPSERVER_EVENT_STOPPED                    = (1 << 1),
        MEDUSA_HTTPSERVER_EVENT_BINDING                    = (1 << 2),
        MEDUSA_HTTPSERVER_EVENT_BOUND                      = (1 << 3),
        MEDUSA_HTTPSERVER_EVENT_LISTENING                  = (1 << 4),
        MEDUSA_HTTPSERVER_EVENT_CONNECTION                 = (1 << 5),
        MEDUSA_HTTPSERVER_EVENT_ERROR                      = (1 << 6),
        MEDUSA_HTTPSERVER_EVENT_DESTROY                    = (1 << 7)
#define MEDUSA_HTTPSERVER_EVENT_STARTED                    MEDUSA_HTTPSERVER_EVENT_STARTED
#define MEDUSA_HTTPSERVER_EVENT_STOPPED                    MEDUSA_HTTPSERVER_EVENT_STOPPED
#define MEDUSA_HTTPSERVER_EVENT_BINDING                    MEDUSA_HTTPSERVER_EVENT_BINDING
#define MEDUSA_HTTPSERVER_EVENT_BOUND                      MEDUSA_HTTPSERVER_EVENT_BOUND
#define MEDUSA_HTTPSERVER_EVENT_LISTENING                  MEDUSA_HTTPSERVER_EVENT_LISTENING
#define MEDUSA_HTTPSERVER_EVENT_CONNECTION                 MEDUSA_HTTPSERVER_EVENT_CONNECTION
#define MEDUSA_HTTPSERVER_EVENT_ERROR                      MEDUSA_HTTPSERVER_EVENT_ERROR
#define MEDUSA_HTTPSERVER_EVENT_DESTROY                    MEDUSA_HTTPSERVER_EVENT_DESTROY
};

enum {
        MEDUSA_HTTPSERVER_STATE_UNKNOWN                    = 0,
        MEDUSA_HTTPSERVER_STATE_STOPPED                    = 1,
        MEDUSA_HTTPSERVER_STATE_STARTED                    = 2,
        MEDUSA_HTTPSERVER_STATE_BINDING                    = 3,
        MEDUSA_HTTPSERVER_STATE_BOUND                      = 4,
        MEDUSA_HTTPSERVER_STATE_LISTENING                  = 5,
        MEDUSA_HTTPSERVER_STATE_ERROR                      = 6
#define MEDUSA_HTTPSERVER_STATE_UNKNOWN                    MEDUSA_HTTPSERVER_STATE_UNKNOWN
#define MEDUSA_HTTPSERVER_STATE_STOPPED                    MEDUSA_HTTPSERVER_STATE_STOPPED
#define MEDUSA_HTTPSERVER_STATE_STARTED                    MEDUSA_HTTPSERVER_STATE_STARTED
#define MEDUSA_HTTPSERVER_STATE_BINDING                    MEDUSA_HTTPSERVER_STATE_BINDING
#define MEDUSA_HTTPSERVER_STATE_BOUND                      MEDUSA_HTTPSERVER_STATE_BOUND
#define MEDUSA_HTTPSERVER_STATE_LISTENING                  MEDUSA_HTTPSERVER_STATE_LISTENING
#define MEDUSA_HTTPSERVER_STATE_ERROR                      MEDUSA_HTTPSERVER_STATE_ERROR
};

enum {
        MEDUSA_HTTPSERVER_CLIENT_EVENT_ERROR                    = (1 <<  0),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_CONNECTED                = (1 <<  1),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_CONNECTED_SSL            = (1 <<  2),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVING        = (1 <<  3),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVED         = (1 <<  4),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVE_TIMEOUT  = (1 <<  5),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE           = (1 <<  6),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE_FINISHED  = (1 <<  7),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE_TIMEOUT   = (1 <<  8),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_REPLY_SENDING            = (1 <<  9),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_REPLY_SENT               = (1 << 10),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_DISCONNECTED             = (1 << 11),
        MEDUSA_HTTPSERVER_CLIENT_EVENT_DESTROY                  = (1 << 12)
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_ERROR                    MEDUSA_HTTPSERVER_CLIENT_EVENT_ERROR
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_CONNECTED                MEDUSA_HTTPSERVER_CLIENT_EVENT_CONNECTED
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_CONNECTED_SSL            MEDUSA_HTTPSERVER_CLIENT_EVENT_CONNECTED_SSL
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVING        MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVING
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVED         MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVED
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVE_TIMEOUT  MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVE_TIMEOUT
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE           MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE_FINISHED  MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE_FINISHED
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE_TIMEOUT   MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE_TIMEOUT
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_REPLY_SENDING            MEDUSA_HTTPSERVER_CLIENT_EVENT_REPLY_SENDING
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_REPLY_SENT               MEDUSA_HTTPSERVER_CLIENT_EVENT_REPLY_SENT
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_DISCONNECTED             MEDUSA_HTTPSERVER_CLIENT_EVENT_DISCONNECTED
#define MEDUSA_HTTPSERVER_CLIENT_EVENT_DESTROY                  MEDUSA_HTTPSERVER_CLIENT_EVENT_DESTROY
};

enum {
        MEDUSA_HTTPSERVER_CLIENT_STATE_UNKNOWN                  = 0,
        MEDUSA_HTTPSERVER_CLIENT_STATE_DISCONNECTED             = 1,
        MEDUSA_HTTPSERVER_CLIENT_STATE_CONNECTED                = 2,
        MEDUSA_HTTPSERVER_CLIENT_STATE_REQUEST_RECEIVING        = 3,
        MEDUSA_HTTPSERVER_CLIENT_STATE_REQUEST_RECEIVED         = 4,
        MEDUSA_HTTPSERVER_CLIENT_STATE_REPLY_SENDING            = 5,
        MEDUSA_HTTPSERVER_CLIENT_STATE_REPLY_SENT               = 6,
        MEDUSA_HTTPSERVER_CLIENT_STATE_ERROR                    = 7
#define MEDUSA_HTTPSERVER_CLIENT_STATE_UNKNOWN                  MEDUSA_HTTPSERVER_CLIENT_STATE_UNKNOWN
#define MEDUSA_HTTPSERVER_CLIENT_STATE_DISCONNECTED             MEDUSA_HTTPSERVER_CLIENT_STATE_DISCONNECTED
#define MEDUSA_HTTPSERVER_CLIENT_STATE_CONNECTED                MEDUSA_HTTPSERVER_CLIENT_STATE_CONNECTED
#define MEDUSA_HTTPSERVER_CLIENT_STATE_REQUEST_RECEIVING        MEDUSA_HTTPSERVER_CLIENT_STATE_REQUEST_RECEIVING
#define MEDUSA_HTTPSERVER_CLIENT_STATE_REQUEST_RECEIVED         MEDUSA_HTTPSERVER_CLIENT_STATE_REQUEST_RECEIVED
#define MEDUSA_HTTPSERVER_CLIENT_STATE_REPLY_SENDING            MEDUSA_HTTPSERVER_CLIENT_STATE_REPLY_SENDING
#define MEDUSA_HTTPSERVER_CLIENT_STATE_REPLY_SENT               MEDUSA_HTTPSERVER_CLIENT_STATE_REPLY_SENT
#define MEDUSA_HTTPSERVER_CLIENT_STATE_ERROR                    MEDUSA_HTTPSERVER_CLIENT_STATE_ERROR
};

struct medusa_httpserver_init_options {
        struct medusa_monitor *monitor;
        unsigned int protocol;
        const char *address;
        unsigned short port;
        int reuseport;
        int backlog;
        int enabled;
        int started;
        int (*onevent) (struct medusa_httpserver *httpserver, unsigned int events, void *context, void *param);
        void *context;
};

struct medusa_httpserver_accept_options {
        int enabled;
        double read_timeout;
        double write_timeout;
        int (*onevent) (struct medusa_httpserver_client *httpserver_client, unsigned int events, void *context, void *param);
        void *context;
};

struct medusa_httpserver_client_event_request_received {
        struct medusa_httpserver_client_request *request;
};

struct medusa_httpserver_client_event_buffered_write {
        int64_t length;
        int64_t remaining;
};

enum {
        MEDUSA_HTTPSERVER_CLIENT_ERROR_REASON_UNKNOWN   = 0,
        MEDUSA_HTTPSERVER_CLIENT_ERROR_REASON_TCPSOCKET = 1
#define MEDUSA_HTTPSERVER_CLIENT_ERROR_REASON_UNKNOWN   MEDUSA_HTTPSERVER_CLIENT_ERROR_REASON_UNKNOWN
#define MEDUSA_HTTPSERVER_CLIENT_ERROR_REASON_TCPSOCKET MEDUSA_HTTPSERVER_CLIENT_ERROR_REASON_TCPSOCKET
};

struct medusa_httpserver_client_event_error {
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
        } u;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_httpserver_init_options_default (struct medusa_httpserver_init_options *options);

struct medusa_httpserver * medusa_httpserver_create (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_httpserver *httpserver, unsigned int events, void *context, void *param), void *context);
struct medusa_httpserver * medusa_httpserver_create_with_options (const struct medusa_httpserver_init_options *options);
void medusa_httpserver_destroy (struct medusa_httpserver *httpserver);

int medusa_httpserver_get_state (const struct medusa_httpserver *httpserver);
int medusa_httpserver_get_error (const struct medusa_httpserver *httpserver);

int medusa_httpserver_get_protocol (struct medusa_httpserver *httpserver);
int medusa_httpserver_get_sockport (const struct medusa_httpserver *httpserver);
int medusa_httpserver_get_sockname (const struct medusa_httpserver *httpserver, struct sockaddr_storage *sockaddr);

int medusa_httpserver_set_enabled (struct medusa_httpserver *httpserver, int enabled);
int medusa_httpserver_get_enabled (const struct medusa_httpserver *httpserver);

int medusa_httpserver_pause (struct medusa_httpserver *httpserver);
int medusa_httpserver_resume (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_started (struct medusa_httpserver *httpserver, int started);
int medusa_httpserver_get_started (const struct medusa_httpserver *httpserver);

int medusa_httpserver_start (struct medusa_httpserver *httpserver);
int medusa_httpserver_stop (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_ssl (struct medusa_httpserver *httpserver, int enable);
int medusa_httpserver_get_ssl (const struct medusa_httpserver *httpserver);

int medusa_httpserver_set_ssl_certificate (struct medusa_httpserver *httpserver, const char *certificate, int length);
int medusa_httpserver_set_ssl_certificate_file (struct medusa_httpserver *httpserver, const char *certificate);
const char * medusa_httpserver_get_ssl_certificate (const struct medusa_httpserver *httpserver);

int medusa_httpserver_set_ssl_privatekey (struct medusa_httpserver *httpserver, const char *privatekey, int length);
int medusa_httpserver_set_ssl_privatekey_file (struct medusa_httpserver *httpserver, const char *privatekey);
const char * medusa_httpserver_get_ssl_privatekey (const struct medusa_httpserver *httpserver);

int medusa_httpserver_set_context (struct medusa_httpserver *httpserver, void *context);
void * medusa_httpserver_get_context (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_userdata (struct medusa_httpserver *httpserver, void *userdata);
void * medusa_httpserver_get_userdata (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_userdata_ptr (struct medusa_httpserver *httpserver, void *userdata);
void * medusa_httpserver_get_userdata_ptr (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_userdata_int (struct medusa_httpserver *httpserver, int userdara);
int medusa_httpserver_get_userdata_int (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_userdata_uint (struct medusa_httpserver *httpserver, unsigned int userdata);
unsigned int medusa_httpserver_get_userdata_uint (struct medusa_httpserver *httpserver);

int medusa_httpserver_onevent (struct medusa_httpserver *httpserver, unsigned int events, void *param);
struct medusa_monitor * medusa_httpserver_get_monitor (struct medusa_httpserver *httpserver);

const char * medusa_httpserver_event_string (unsigned int events);
const char * medusa_httpserver_state_string (unsigned int state);

int medusa_httpserver_accept_options_default (struct medusa_httpserver_accept_options *options);

struct medusa_httpserver_client * medusa_httpserver_accept (struct medusa_httpserver *httpserver, int (*onevent) (struct medusa_httpserver_client *httpserver_client, unsigned int events, void *context, void *param), void *context);
struct medusa_httpserver_client * medusa_httpserver_accept_with_options (struct medusa_httpserver *httpserver, struct medusa_httpserver_accept_options *options);
void medusa_httpserver_client_destroy (struct medusa_httpserver_client *httpserver_client);

unsigned int medusa_httpserver_client_get_state (const struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_enabled (struct medusa_httpserver_client *httpserver_client, int enabled);
int medusa_httpserver_client_get_enabled (const struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_read_timeout (struct medusa_httpserver_client *httpserver_client, double timeout);
double medusa_httpserver_client_get_read_timeout (const struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_write_timeout (struct medusa_httpserver_client *httpserver_client, double timeout);
double medusa_httpserver_client_get_write_timeout (const struct medusa_httpserver_client *httpserver_client);

const struct medusa_httpserver_client_request * medusa_httprequest_client_get_request (const struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_request_get_http_major (const struct medusa_httpserver_client_request *request);
int medusa_httpserver_client_request_get_http_minor (const struct medusa_httpserver_client_request *request);
const char * medusa_httpserver_client_request_get_method (const struct medusa_httpserver_client_request *request);
const char * medusa_httpserver_client_request_get_url (const struct medusa_httpserver_client_request *request);
const char * medusa_httpserver_client_request_get_path (const struct medusa_httpserver_client_request *request);

const struct medusa_httpserver_client_request_options * medusa_httpserver_client_request_get_options (const struct medusa_httpserver_client_request *request);
int64_t medusa_httpserver_client_request_options_get_count (const struct medusa_httpserver_client_request_options *options);
const struct medusa_httpserver_client_request_option * medusa_httpserver_client_request_options_get_first (const struct medusa_httpserver_client_request_options *options);

const char * medusa_httpserver_client_request_option_get_key (const struct medusa_httpserver_client_request_option *option);
const char * medusa_httpserver_client_request_option_get_value (const struct medusa_httpserver_client_request_option *option);
const struct medusa_httpserver_client_request_option * medusa_httpserver_client_request_option_get_next (const struct medusa_httpserver_client_request_option *option);

const struct medusa_httpserver_client_request_headers * medusa_httpserver_client_request_get_headers (const struct medusa_httpserver_client_request *request);
int64_t medusa_httpserver_client_request_headers_get_count (const struct medusa_httpserver_client_request_headers *headers);
const struct medusa_httpserver_client_request_header * medusa_httpserver_client_request_headers_get_first (const struct medusa_httpserver_client_request_headers *headers);

const char * medusa_httpserver_client_request_header_get_key (const struct medusa_httpserver_client_request_header *header);
const char * medusa_httpserver_client_request_header_get_value (const struct medusa_httpserver_client_request_header *header);
const struct medusa_httpserver_client_request_header * medusa_httpserver_client_request_header_get_next (const struct medusa_httpserver_client_request_header *header);

const struct medusa_httpserver_client_request_body * medusa_httpserver_client_request_get_body (const struct medusa_httpserver_client_request *request);
int64_t medusa_httpserver_client_request_body_get_length (const struct medusa_httpserver_client_request_body *body);
const void * medusa_httpserver_client_request_body_get_value (const struct medusa_httpserver_client_request_body *body);

int medusa_httpserver_client_reply_send_start (struct medusa_httpserver_client *httpserver_client);
int medusa_httpserver_client_reply_send_status (struct medusa_httpserver_client *httpserver_client, const char *version, int code, const char *reason);
int medusa_httpserver_client_reply_send_statusf (struct medusa_httpserver_client *httpserver_client, const char *version, int code, const char *reason, ...) __attribute__((format(printf, 4, 5)));
int medusa_httpserver_client_reply_send_statusv (struct medusa_httpserver_client *httpserver_client, const char *version, int code, const char *reason, va_list va);
int medusa_httpserver_client_reply_send_header (struct medusa_httpserver_client *httpserver_client, const char *key, const char *value);
int medusa_httpserver_client_reply_send_headerf (struct medusa_httpserver_client *httpserver_client, const char *key, const char *value, ...) __attribute__((format(printf, 3, 4)));
int medusa_httpserver_client_reply_send_headerv (struct medusa_httpserver_client *httpserver_client, const char *key, const char *value, va_list va);
int medusa_httpserver_client_reply_send_body (struct medusa_httpserver_client *httpserver_client, const void *body, int length);
int medusa_httpserver_client_reply_send_bodyf (struct medusa_httpserver_client *httpserver_client, const char *body, ...) __attribute__((format(printf, 2, 3)));
int medusa_httpserver_client_reply_send_bodyv (struct medusa_httpserver_client *httpserver_client, const char *body, va_list va);
int medusa_httpserver_client_reply_send_finish (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_get_fd (struct medusa_httpserver_client *httpserver_client);
int medusa_httpserver_client_get_sockname (struct medusa_httpserver_client *httpserver_client, struct sockaddr_storage *sockaddr);
int medusa_httpserver_client_get_peername (struct medusa_httpserver_client *httpserver_client, struct sockaddr_storage *sockaddr);

int medusa_httpserver_client_set_context (struct medusa_httpserver_client *httpserver_client, void *context);
void * medusa_httpserver_client_get_context (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_userdata (struct medusa_httpserver_client *httpserver_client, void *userdata);
void * medusa_httpserver_client_get_userdata (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_userdata_ptr (struct medusa_httpserver_client *httpserver_client, void *userdata);
void * medusa_httpserver_client_get_userdata_ptr (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_userdata_int (struct medusa_httpserver_client *httpserver_client, int userdara);
int medusa_httpserver_client_get_userdata_int (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_userdata_uint (struct medusa_httpserver_client *httpserver_client, unsigned int userdata);
unsigned int medusa_httpserver_client_get_userdata_uint (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_onevent (struct medusa_httpserver_client *httpserver_client, unsigned int events, void *param);
struct medusa_monitor * medusa_httpserver_client_get_monitor (struct medusa_httpserver_client *httpserver_client);

const char * medusa_httpserver_client_event_string (unsigned int events);
const char * medusa_httpserver_client_state_string (unsigned int state);

#ifdef __cplusplus
}
#endif

#endif
