
#if !defined(MEDUSA_WEBSOCKETSERVER_H)
#define MEDUSA_WEBSOCKETSERVER_H

struct sockaddr_storage;

struct medusa_monitor;
struct medusa_websocketserver;
struct medusa_websocketserver_client;

enum {
        MEDUSA_WEBSOCKETSERVER_PROTOCOL_ANY                     = 0,
        MEDUSA_WEBSOCKETSERVER_PROTOCOL_IPV4                    = 1,
        MEDUSA_WEBSOCKETSERVER_PROTOCOL_IPV6                    = 2
#define MEDUSA_WEBSOCKETSERVER_PROTOCOL_ANY                     MEDUSA_WEBSOCKETSERVER_PROTOCOL_ANY
#define MEDUSA_WEBSOCKETSERVER_PROTOCOL_IPV4                    MEDUSA_WEBSOCKETSERVER_PROTOCOL_IPV4
#define MEDUSA_WEBSOCKETSERVER_PROTOCOL_IPV6                    MEDUSA_WEBSOCKETSERVER_PROTOCOL_IPV6
};

enum {
        MEDUSA_WEBSOCKETSERVER_EVENT_STARTED                    = (1 << 0),
        MEDUSA_WEBSOCKETSERVER_EVENT_STOPPED                    = (1 << 1),
        MEDUSA_WEBSOCKETSERVER_EVENT_BINDING                    = (1 << 2),
        MEDUSA_WEBSOCKETSERVER_EVENT_BOUND                      = (1 << 3),
        MEDUSA_WEBSOCKETSERVER_EVENT_LISTENING                  = (1 << 4),
        MEDUSA_WEBSOCKETSERVER_EVENT_CONNECTION                 = (1 << 5),
        MEDUSA_WEBSOCKETSERVER_EVENT_ERROR                      = (1 << 6),
        MEDUSA_WEBSOCKETSERVER_EVENT_DESTROY                    = (1 << 7)
#define MEDUSA_WEBSOCKETSERVER_EVENT_STARTED                    MEDUSA_WEBSOCKETSERVER_EVENT_STARTED
#define MEDUSA_WEBSOCKETSERVER_EVENT_STOPPED                    MEDUSA_WEBSOCKETSERVER_EVENT_STOPPED
#define MEDUSA_WEBSOCKETSERVER_EVENT_BINDING                    MEDUSA_WEBSOCKETSERVER_EVENT_BINDING
#define MEDUSA_WEBSOCKETSERVER_EVENT_BOUND                      MEDUSA_WEBSOCKETSERVER_EVENT_BOUND
#define MEDUSA_WEBSOCKETSERVER_EVENT_LISTENING                  MEDUSA_WEBSOCKETSERVER_EVENT_LISTENING
#define MEDUSA_WEBSOCKETSERVER_EVENT_CONNECTION                 MEDUSA_WEBSOCKETSERVER_EVENT_CONNECTION
#define MEDUSA_WEBSOCKETSERVER_EVENT_ERROR                      MEDUSA_WEBSOCKETSERVER_EVENT_ERROR
#define MEDUSA_WEBSOCKETSERVER_EVENT_DESTROY                    MEDUSA_WEBSOCKETSERVER_EVENT_DESTROY
};

enum {
        MEDUSA_WEBSOCKETSERVER_STATE_UNKNOWN                    = 0,
        MEDUSA_WEBSOCKETSERVER_STATE_STOPPED                    = 1,
        MEDUSA_WEBSOCKETSERVER_STATE_STARTED                    = 2,
        MEDUSA_WEBSOCKETSERVER_STATE_BINDING                    = 3,
        MEDUSA_WEBSOCKETSERVER_STATE_BOUND                      = 4,
        MEDUSA_WEBSOCKETSERVER_STATE_LISTENING                  = 5,
        MEDUSA_WEBSOCKETSERVER_STATE_ERROR                      = 6
#define MEDUSA_WEBSOCKETSERVER_STATE_UNKNOWN                    MEDUSA_WEBSOCKETSERVER_STATE_UNKNOWN
#define MEDUSA_WEBSOCKETSERVER_STATE_STOPPED                    MEDUSA_WEBSOCKETSERVER_STATE_STOPPED
#define MEDUSA_WEBSOCKETSERVER_STATE_STARTED                    MEDUSA_WEBSOCKETSERVER_STATE_STARTED
#define MEDUSA_WEBSOCKETSERVER_STATE_BINDING                    MEDUSA_WEBSOCKETSERVER_STATE_BINDING
#define MEDUSA_WEBSOCKETSERVER_STATE_BOUND                      MEDUSA_WEBSOCKETSERVER_STATE_BOUND
#define MEDUSA_WEBSOCKETSERVER_STATE_LISTENING                  MEDUSA_WEBSOCKETSERVER_STATE_LISTENING
#define MEDUSA_WEBSOCKETSERVER_STATE_ERROR                      MEDUSA_WEBSOCKETSERVER_STATE_ERROR
};

enum {
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_ERROR                       = (1 <<  0),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_ACCEPTED                    = (1 <<  1),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_RECEIVING           = (1 <<  2),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_HEADER              = (1 <<  3),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_RECEIVED            = (1 <<  4),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_CONNECTED                   = (1 <<  5),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_MESSAGE                     = (1 <<  6),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_BUFFERED_WRITE              = (1 <<  7),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_BUFFERED_WRITE_FINISHED     = (1 <<  8),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_DISCONNECTED                = (1 <<  9),
        MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_DESTROY                     = (1 << 10)
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_ERROR                       MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_ERROR
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_ACCEPTED                    MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_ACCEPTED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_RECEIVING           MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_RECEIVING
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_HEADER              MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_HEADER
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_RECEIVED            MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_RECEIVED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_CONNECTED                   MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_CONNECTED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_MESSAGE                     MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_MESSAGE
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_BUFFERED_WRITE              MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_BUFFERED_WRITE
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_BUFFERED_WRITE_FINISHED     MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_BUFFERED_WRITE_FINISHED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_DISCONNECTED                MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_DISCONNECTED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_DESTROY                     MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_DESTROY
};

enum {
        MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_UNKNOWN             = 0,
        MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_DISCONNECTED        = 1,
        MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_ACCEPTED            = 2,
        MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_REQUEST_RECEIVING   = 3,
        MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_REQUEST_RECEIVED    = 4,
        MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_CONNECTED           = 5,
        MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_ERROR               = 6
#define MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_UNKNOWN             MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_UNKNOWN
#define MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_DISCONNECTED        MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_DISCONNECTED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_ACCEPTED            MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_ACCEPTED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_REQUEST_RECEIVING   MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_REQUEST_RECEIVING
#define MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_REQUEST_RECEIVED    MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_REQUEST_RECEIVED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_CONNECTED           MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_CONNECTED
#define MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_ERROR               MEDUSA_WEBSOCKETSERVER_CLIENT_STATE_ERROR
};

enum {
        MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_CONTINUATION   = 0,
        MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_CLOSE          = 1,
        MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_PING           = 2,
        MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_PONG           = 3,
        MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_TEXT           = 4,
        MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_BINARY         = 5
#define MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_CONTINUATION   MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_CONTINUATION
#define MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_CLOSE          MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_CLOSE
#define MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_PING           MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_PING
#define MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_PONG           MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_PONG
#define MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_TEXT           MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_TEXT
#define MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_BINARY         MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_BINARY
};

struct medusa_websocketserver_init_options {
        struct medusa_monitor *monitor;
        unsigned int protocol;
        const char *address;
        unsigned short port;
        const char *servername;
        int reuseport;
        int backlog;
        int enabled;
        int started;
        int (*onevent) (struct medusa_websocketserver *websocketserver, unsigned int events, void *context, void *param);
        void *context;
};

struct medusa_websocketserver_accept_options {
        int enabled;
        double read_timeout;
        double write_timeout;
        int (*onevent) (struct medusa_websocketserver_client *websocketserver_client, unsigned int events, void *context, void *param);
        void *context;
};

struct medusa_websocketserver_client_event_request_header {
        const char *field;
        const char *value;
};

struct medusa_websocketserver_client_event_message {
        unsigned int final;
        unsigned int type;
        unsigned int length;
        const void *payload;
};

struct medusa_websocketserver_client_event_buffered_write {
        int64_t length;
        int64_t remaining;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_websocketserver_init_options_default (struct medusa_websocketserver_init_options *options);

struct medusa_websocketserver * medusa_websocketserver_create (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_websocketserver *websocketserver, unsigned int events, void *context, void *param), void *context);
struct medusa_websocketserver * medusa_websocketserver_create_with_options (const struct medusa_websocketserver_init_options *options);
void medusa_websocketserver_destroy (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_get_state (const struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_get_error (const struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_get_protocol (struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_get_sockport (const struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_get_sockname (const struct medusa_websocketserver *websocketserver, struct sockaddr_storage *sockaddr);

int medusa_websocketserver_set_enabled (struct medusa_websocketserver *websocketserver, int enabled);
int medusa_websocketserver_get_enabled (const struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_pause (struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_resume (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_started (struct medusa_websocketserver *websocketserver, int started);
int medusa_websocketserver_get_started (const struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_start (struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_stop (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_context (struct medusa_websocketserver *websocketserver, void *context);
void * medusa_websocketserver_get_context (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_userdata (struct medusa_websocketserver *websocketserver, void *userdata);
void * medusa_websocketserver_get_userdata (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_userdata_ptr (struct medusa_websocketserver *websocketserver, void *userdata);
void * medusa_websocketserver_get_userdata_ptr (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_userdata_int (struct medusa_websocketserver *websocketserver, int userdara);
int medusa_websocketserver_get_userdata_int (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_userdata_uint (struct medusa_websocketserver *websocketserver, unsigned int userdata);
unsigned int medusa_websocketserver_get_userdata_uint (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_onevent (struct medusa_websocketserver *websocketserver, unsigned int events, void *param);
struct medusa_monitor * medusa_websocketserver_get_monitor (struct medusa_websocketserver *websocketserver);

const char * medusa_websocketserver_event_string (unsigned int events);
const char * medusa_websocketserver_state_string (unsigned int state);

int medusa_websocketserver_accept_options_default (struct medusa_websocketserver_accept_options *options);

struct medusa_websocketserver_client * medusa_websocketserver_accept (struct medusa_websocketserver *websocketserver, int (*onevent) (struct medusa_websocketserver_client *websocketserver_client, unsigned int events, void *context, void *param), void *context);
struct medusa_websocketserver_client * medusa_websocketserver_accept_with_options (struct medusa_websocketserver *websocketserver, struct medusa_websocketserver_accept_options *options);
void medusa_websocketserver_client_destroy (struct medusa_websocketserver_client *websocketserver_client);

unsigned int medusa_websocketserver_client_get_state (const struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_enabled (struct medusa_websocketserver_client *websocketserver_client, int enabled);
int medusa_websocketserver_client_get_enabled (const struct medusa_websocketserver_client *websocketserver_client);

struct medusa_buffer * medusa_websocketserver_client_get_read_buffer (const struct medusa_websocketserver_client *websocketserver_client);
struct medusa_buffer * medusa_websocketserver_client_get_write_buffer (const struct medusa_websocketserver_client *websocketserver_client);

int64_t medusa_websocketserver_client_write (struct medusa_websocketserver_client *websocketserver_client, unsigned int final, unsigned int type, const void *data, int64_t length);

int medusa_websocketserver_client_get_sockname (struct medusa_websocketserver_client *websocketserver_client, struct sockaddr_storage *sockaddr);
int medusa_websocketserver_client_get_peername (struct medusa_websocketserver_client *websocketserver_client, struct sockaddr_storage *sockaddr);

int medusa_websocketserver_client_set_context (struct medusa_websocketserver_client *websocketserver_client, void *context);
void * medusa_websocketserver_client_get_context (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_userdata (struct medusa_websocketserver_client *websocketserver_client, void *userdata);
void * medusa_websocketserver_client_get_userdata (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_userdata_ptr (struct medusa_websocketserver_client *websocketserver_client, void *userdata);
void * medusa_websocketserver_client_get_userdata_ptr (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_userdata_int (struct medusa_websocketserver_client *websocketserver_client, int userdara);
int medusa_websocketserver_client_get_userdata_int (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_userdata_uint (struct medusa_websocketserver_client *websocketserver_client, unsigned int userdata);
unsigned int medusa_websocketserver_client_get_userdata_uint (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_onevent (struct medusa_websocketserver_client *websocketserver_client, unsigned int events, void *param);
struct medusa_monitor * medusa_websocketserver_client_get_monitor (struct medusa_websocketserver_client *websocketserver_client);

const char * medusa_websocketserver_client_event_string (unsigned int events);
const char * medusa_websocketserver_client_state_string (unsigned int state);
const char * medusa_websocketserver_client_frame_type_string (unsigned int type);

#ifdef __cplusplus
}
#endif

#endif
