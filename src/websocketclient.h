
#if !defined(MEDUSA_WEBSOCKETCLIENT_H)
#define MEDUSA_WEBSOCKETCLIENT_H

struct sockaddr_storage;

struct medusa_monitor;
struct medusa_websocketclient;

enum {
        MEDUSA_WEBSOCKETCLIENT_PROTOCOL_ANY                     = 0,
        MEDUSA_WEBSOCKETCLIENT_PROTOCOL_IPV4                    = 1,
        MEDUSA_WEBSOCKETCLIENT_PROTOCOL_IPV6                    = 2
#define MEDUSA_WEBSOCKETCLIENT_PROTOCOL_ANY                     MEDUSA_WEBSOCKETCLIENT_PROTOCOL_ANY
#define MEDUSA_WEBSOCKETCLIENT_PROTOCOL_IPV4                    MEDUSA_WEBSOCKETCLIENT_PROTOCOL_IPV4
#define MEDUSA_WEBSOCKETCLIENT_PROTOCOL_IPV6                    MEDUSA_WEBSOCKETCLIENT_PROTOCOL_IPV6
};

enum {
        MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR                      = (1 <<  0),
        MEDUSA_WEBSOCKETCLIENT_EVENT_SENDING_REQUEST            = (1 <<  1),
        MEDUSA_WEBSOCKETCLIENT_EVENT_REQUEST_SENT               = (1 <<  2),
        MEDUSA_WEBSOCKETCLIENT_EVENT_RECEIVING_RESPONSE         = (1 <<  3),
        MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_HEADER            = (1 <<  4),
        MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_RECEIVED          = (1 <<  5),
        MEDUSA_WEBSOCKETCLIENT_EVENT_CONNECTED                  = (1 <<  6),
        MEDUSA_WEBSOCKETCLIENT_EVENT_MESSAGE                    = (1 <<  7),
        MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE             = (1 <<  8),
        MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE_FINISHED    = (1 <<  9),
        MEDUSA_WEBSOCKETCLIENT_EVENT_DISCONNECTED               = (1 << 10),
        MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY                    = (1 << 11)
#define MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR                      MEDUSA_WEBSOCKETCLIENT_EVENT_ERROR
#define MEDUSA_WEBSOCKETCLIENT_EVENT_SENDING_REQUEST            MEDUSA_WEBSOCKETCLIENT_EVENT_SENDING_REQUEST
#define MEDUSA_WEBSOCKETCLIENT_EVENT_REQUEST_SENT               MEDUSA_WEBSOCKETCLIENT_EVENT_REQUEST_SENT
#define MEDUSA_WEBSOCKETCLIENT_EVENT_RECEIVING_RESPONSE         MEDUSA_WEBSOCKETCLIENT_EVENT_RECEIVING_RESPONSE
#define MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_HEADER            MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_HEADER
#define MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_RECEIVED          MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_RECEIVED
#define MEDUSA_WEBSOCKETCLIENT_EVENT_CONNECTED                  MEDUSA_WEBSOCKETCLIENT_EVENT_CONNECTED
#define MEDUSA_WEBSOCKETCLIENT_EVENT_MESSAGE                    MEDUSA_WEBSOCKETCLIENT_EVENT_MESSAGE
#define MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE             MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE
#define MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE_FINISHED    MEDUSA_WEBSOCKETCLIENT_EVENT_BUFFERED_WRITE_FINISHED
#define MEDUSA_WEBSOCKETCLIENT_EVENT_DISCONNECTED               MEDUSA_WEBSOCKETCLIENT_EVENT_DISCONNECTED
#define MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY                    MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY
};

enum {
        MEDUSA_WEBSOCKETCLIENT_STATE_UNKNOWN            = 0,
        MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED       = 1,
        MEDUSA_WEBSOCKETCLIENT_STATE_SENDING_REQUEST    = 2,
        MEDUSA_WEBSOCKETCLIENT_STATE_REQUEST_SENT       = 3,
        MEDUSA_WEBSOCKETCLIENT_STATE_RECEIVING_RESPONSE = 4,
        MEDUSA_WEBSOCKETCLIENT_STATE_RESPONSE_RECEIVED  = 5,
        MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED          = 6,
        MEDUSA_WEBSOCKETCLIENT_STATE_ERROR              = 7
#define MEDUSA_WEBSOCKETCLIENT_STATE_UNKNOWN            MEDUSA_WEBSOCKETCLIENT_STATE_UNKNOWN
#define MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED       MEDUSA_WEBSOCKETCLIENT_STATE_DISCONNECTED
#define MEDUSA_WEBSOCKETCLIENT_STATE_SENDING_REQUEST    MEDUSA_WEBSOCKETCLIENT_STATE_SENDING_REQUEST
#define MEDUSA_WEBSOCKETCLIENT_STATE_REQUEST_SENT       MEDUSA_WEBSOCKETCLIENT_STATE_REQUEST_SENT
#define MEDUSA_WEBSOCKETCLIENT_STATE_RECEIVING_RESPONSE MEDUSA_WEBSOCKETCLIENT_STATE_RECEIVING_RESPONSE
#define MEDUSA_WEBSOCKETCLIENT_STATE_RESPONSE_RECEIVED  MEDUSA_WEBSOCKETCLIENT_STATE_RESPONSE_RECEIVED
#define MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED          MEDUSA_WEBSOCKETCLIENT_STATE_CONNECTED
#define MEDUSA_WEBSOCKETCLIENT_STATE_ERROR              MEDUSA_WEBSOCKETCLIENT_STATE_ERROR
};

enum {
        MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CONTINUATION  = 0,
        MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CLOSE         = 1,
        MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PING          = 2,
        MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PONG          = 3,
        MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT          = 4,
        MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_BINARY        = 5
#define MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CONTINUATION  MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CONTINUATION
#define MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CLOSE         MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_CLOSE
#define MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PING          MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PING
#define MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PONG          MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_PONG
#define MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT          MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT
#define MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_BINARY        MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_BINARY
};

struct medusa_websocketclient_connect_options {
        struct medusa_monitor *monitor;
        unsigned int protocol;
        const char *address;
        unsigned short port;
        const char *server_path;
        const char *server_protocol;
        int enabled;
        int (*onevent) (struct medusa_websocketclient *websocketclient, unsigned int events, void *context, void *param);
        void *context;
};

struct medusa_websocketclient_event_response_header {
        const char *field;
        const char *value;
};

struct medusa_websocketclient_event_message {
        unsigned int final;
        unsigned int type;
        unsigned int length;
        const void *payload;
};

struct medusa_websocketclient_event_buffered_write {
        int64_t length;
        int64_t remaining;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_websocketclient_connect_options_default (struct medusa_websocketclient_connect_options *options);

struct medusa_websocketclient * medusa_websocketclient_connect (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_websocketclient *websocketclient, unsigned int events, void *context, void *param), void *context);
struct medusa_websocketclient * medusa_websocketclient_connect_with_options (const struct medusa_websocketclient_connect_options *options);
void medusa_websocketclient_destroy (struct medusa_websocketclient *websocketclient);

unsigned int medusa_websocketclient_get_state (const struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_enabled (struct medusa_websocketclient *websocketclient, int enabled);
int medusa_websocketclient_get_enabled (const struct medusa_websocketclient *websocketclient);

struct medusa_buffer * medusa_websocketclient_get_read_buffer (const struct medusa_websocketclient *websocketclient);
struct medusa_buffer * medusa_websocketclient_get_write_buffer (const struct medusa_websocketclient *websocketclient);

int64_t medusa_websocketclient_write (struct medusa_websocketclient *websocketclient, unsigned int final, unsigned int type, const void *data, int64_t length);

int medusa_websocketclient_get_sockname (struct medusa_websocketclient *websocketclient, struct sockaddr_storage *sockaddr);
int medusa_websocketclient_get_peername (struct medusa_websocketclient *websocketclient, struct sockaddr_storage *sockaddr);

int medusa_websocketclient_set_context (struct medusa_websocketclient *websocketclient, void *context);
void * medusa_websocketclient_get_context (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_userdata (struct medusa_websocketclient *websocketclient, void *userdata);
void * medusa_websocketclient_get_userdata (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_userdata_ptr (struct medusa_websocketclient *websocketclient, void *userdata);
void * medusa_websocketclient_get_userdata_ptr (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_userdata_int (struct medusa_websocketclient *websocketclient, int userdara);
int medusa_websocketclient_get_userdata_int (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_userdata_uint (struct medusa_websocketclient *websocketclient, unsigned int userdata);
unsigned int medusa_websocketclient_get_userdata_uint (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_onevent (struct medusa_websocketclient *websocketclient, unsigned int events, void *param);
struct medusa_monitor * medusa_websocketclient_get_monitor (struct medusa_websocketclient *websocketclient);

const char * medusa_websocketclient_event_string (unsigned int events);
const char * medusa_websocketclient_state_string (unsigned int state);
const char * medusa_websocketclient_frame_type_string (unsigned int type);

#ifdef __cplusplus
}
#endif

#endif
