
#if !defined(MEDUSA_UDPSOCKET_H)
#define MEDUSA_UDPSOCKET_H

struct sockaddr_storage;

struct medusa_monitor;
struct medusa_udpsocket;

enum {
        MEDUSA_UDPSOCKET_PROTOCOL_ANY                   = 0,
        MEDUSA_UDPSOCKET_PROTOCOL_IPV4                  = 1,
        MEDUSA_UDPSOCKET_PROTOCOL_IPV6                  = 2
#define MEDUSA_UDPSOCKET_PROTOCOL_ANY                   MEDUSA_UDPSOCKET_PROTOCOL_ANY
#define MEDUSA_UDPSOCKET_PROTOCOL_IPV4                  MEDUSA_UDPSOCKET_PROTOCOL_IPV4
#define MEDUSA_UDPSOCKET_PROTOCOL_IPV6                  MEDUSA_UDPSOCKET_PROTOCOL_IPV6
};

enum {
        MEDUSA_UDPSOCKET_EVENT_BINDING                  = (1 <<  0), /* 0x00000001 */
        MEDUSA_UDPSOCKET_EVENT_BOUND                    = (1 <<  1), /* 0x00000002 */
        MEDUSA_UDPSOCKET_EVENT_LISTENING                = (1 <<  2), /* 0x00000004 */
        MEDUSA_UDPSOCKET_EVENT_RESOLVING                = (1 <<  3), /* 0x00000008 */
        MEDUSA_UDPSOCKET_EVENT_RESOLVE_TIMEOUT          = (1 <<  4), /* 0x00000010 */
        MEDUSA_UDPSOCKET_EVENT_RESOLVED                 = (1 <<  5), /* 0x00000020 */
        MEDUSA_UDPSOCKET_EVENT_CONNECTING               = (1 <<  6), /* 0x00000040 */
        MEDUSA_UDPSOCKET_EVENT_CONNECT_TIMEOUT          = (1 <<  7), /* 0x00000080 */
        MEDUSA_UDPSOCKET_EVENT_CONNECTED                = (1 <<  8), /* 0x00000100 */
        MEDUSA_UDPSOCKET_EVENT_IN                       = (1 <<  9), /* 0x00000200 */
        MEDUSA_UDPSOCKET_EVENT_IN_TIMEOUT               = (1 << 10), /* 0x00000400 */
        MEDUSA_UDPSOCKET_EVENT_OUT                      = (1 << 11), /* 0x00000800 */
        MEDUSA_UDPSOCKET_EVENT_DISCONNECTED             = (1 << 12), /* 0x00001000 */
        MEDUSA_UDPSOCKET_EVENT_ERROR                    = (1 << 13), /* 0x00002000 */
        MEDUSA_UDPSOCKET_EVENT_STATE_CHANGED            = (1 << 14), /* 0x00008000 */
        MEDUSA_UDPSOCKET_EVENT_DESTROY                  = (1 << 15)  /* 0x00010000 */
#define MEDUSA_UDPSOCKET_EVENT_BINDING                  MEDUSA_UDPSOCKET_EVENT_BINDING
#define MEDUSA_UDPSOCKET_EVENT_BOUND                    MEDUSA_UDPSOCKET_EVENT_BOUND
#define MEDUSA_UDPSOCKET_EVENT_LISTENING                MEDUSA_UDPSOCKET_EVENT_LISTENING
#define MEDUSA_UDPSOCKET_EVENT_RESOLVING                MEDUSA_UDPSOCKET_EVENT_RESOLVING
#define MEDUSA_UDPSOCKET_EVENT_RESOLVE_TIMEOUT          MEDUSA_UDPSOCKET_EVENT_RESOLVE_TIMEOUT
#define MEDUSA_UDPSOCKET_EVENT_RESOLVED                 MEDUSA_UDPSOCKET_EVENT_RESOLVED
#define MEDUSA_UDPSOCKET_EVENT_CONNECTING               MEDUSA_UDPSOCKET_EVENT_CONNECTING
#define MEDUSA_UDPSOCKET_EVENT_CONNECT_TIMEOUT          MEDUSA_UDPSOCKET_EVENT_CONNECT_TIMEOUT
#define MEDUSA_UDPSOCKET_EVENT_CONNECTED                MEDUSA_UDPSOCKET_EVENT_CONNECTED
#define MEDUSA_UDPSOCKET_EVENT_IN                       MEDUSA_UDPSOCKET_EVENT_IN
#define MEDUSA_UDPSOCKET_EVENT_OUT                      MEDUSA_UDPSOCKET_EVENT_OUT
#define MEDUSA_UDPSOCKET_EVENT_DISCONNECTED             MEDUSA_UDPSOCKET_EVENT_DISCONNECTED
#define MEDUSA_UDPSOCKET_EVENT_ERROR                    MEDUSA_UDPSOCKET_EVENT_ERROR
#define MEDUSA_UDPSOCKET_EVENT_STATE_CHANGED            MEDUSA_UDPSOCKET_EVENT_STATE_CHANGED
#define MEDUSA_UDPSOCKET_EVENT_DESTROY                  MEDUSA_UDPSOCKET_EVENT_DESTROY
};

enum {
        MEDUSA_UDPSOCKET_STATE_UNKNOWN                  = 0,
        MEDUSA_UDPSOCKET_STATE_DISCONNECTED             = 1,
        MEDUSA_UDPSOCKET_STATE_BINDING                  = 2,
        MEDUSA_UDPSOCKET_STATE_BOUND                    = 3,
        MEDUSA_UDPSOCKET_STATE_LISTENING                = 4,
        MEDUSA_UDPSOCKET_STATE_RESOLVING                = 5,
        MEDUSA_UDPSOCKET_STATE_RESOLVED                 = 6,
        MEDUSA_UDPSOCKET_STATE_CONNECTING               = 7,
        MEDUSA_UDPSOCKET_STATE_CONNECTED                = 8,
        MEDUSA_UDPSOCKET_STATE_ERROR                    = 9
#define MEDUSA_UDPSOCKET_STATE_UNKNOWN                  MEDUSA_UDPSOCKET_STATE_UNKNOWN
#define MEDUSA_UDPSOCKET_STATE_BINDING                  MEDUSA_UDPSOCKET_STATE_BINDING
#define MEDUSA_UDPSOCKET_STATE_BOUND                    MEDUSA_UDPSOCKET_STATE_BOUND
#define MEDUSA_UDPSOCKET_STATE_LISTENING                MEDUSA_UDPSOCKET_STATE_LISTENING
#define MEDUSA_UDPSOCKET_STATE_DISCONNECTED             MEDUSA_UDPSOCKET_STATE_DISCONNECTED
#define MEDUSA_UDPSOCKET_STATE_RESOLVING                MEDUSA_UDPSOCKET_STATE_RESOLVING
#define MEDUSA_UDPSOCKET_STATE_RESOLVED                 MEDUSA_UDPSOCKET_STATE_RESOLVED
#define MEDUSA_UDPSOCKET_STATE_CONNECTING               MEDUSA_UDPSOCKET_STATE_CONNECTING
#define MEDUSA_UDPSOCKET_STATE_CONNECTED                MEDUSA_UDPSOCKET_STATE_CONNECTED
#define MEDUSA_UDPSOCKET_STATE_ERROR                    MEDUSA_UDPSOCKET_STATE_ERROR
};

struct medusa_udpsocket_bind_options {
        struct medusa_monitor *monitor;
        int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param);
        void *context;
        unsigned int protocol;
        const char *address;
        unsigned short port;
        int nonblocking;
        int reuseaddr;
        int reuseport;
        int freebind;
        int enabled;
};

struct medusa_udpsocket_open_options {
        struct medusa_monitor *monitor;
        int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param);
        void *context;
        unsigned int protocol;
        int nonblocking;
        int enabled;
};

struct medusa_udpsocket_connect_options {
        struct medusa_monitor *monitor;
        int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param);
        void *context;
        unsigned int protocol;
        const char *address;
        unsigned short port;
        int nonblocking;
        int enabled;
};

struct medusa_udpsocket_attach_options {
        struct medusa_monitor *monitor;
        int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param);
        void *context;
        int fd;
        int bound;
        int clodestroy;
        int nonblocking;
        int enabled;
};

struct medusa_udpsocket_event_error {
        unsigned int state;
        unsigned int error;
        unsigned int line;
};

struct medusa_udpsocket_event_state_changed {
        unsigned int pstate;
        unsigned int state;
        unsigned int error;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_udpsocket_bind_options_default (struct medusa_udpsocket_bind_options *options);
struct medusa_udpsocket * medusa_udpsocket_bind (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_udpsocket * medusa_udpsocket_bind_with_options (const struct medusa_udpsocket_bind_options *options);

int medusa_udpsocket_open_options_default (struct medusa_udpsocket_open_options *options);
struct medusa_udpsocket * medusa_udpsocket_open (struct medusa_monitor *monitor, unsigned int protocol, int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_udpsocket * medusa_udpsocket_open_with_options (const struct medusa_udpsocket_open_options *options);

int medusa_udpsocket_connect_options_default (struct medusa_udpsocket_connect_options *options);
struct medusa_udpsocket * medusa_udpsocket_connect (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_udpsocket * medusa_udpsocket_connect_with_options (const struct medusa_udpsocket_connect_options *options);

int medusa_udpsocket_attach_options_default (struct medusa_udpsocket_attach_options *options);
struct medusa_udpsocket * medusa_udpsocket_attach (struct medusa_monitor *monitor, int fd, int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_udpsocket * medusa_udpsocket_attach_with_options (const struct medusa_udpsocket_attach_options *options);

void medusa_udpsocket_destroy (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_get_state (const struct medusa_udpsocket *udpsocket);
int medusa_udpsocket_get_error (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_enabled (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_enabled (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_enable (struct medusa_udpsocket *udpsocket);
int medusa_udpsocket_disable (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_nonblocking (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_nonblocking (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_reuseaddr (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_reuseaddr (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_reuseport (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_reuseport (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_freebind (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_freebind (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_read_timeout (struct medusa_udpsocket *udpsocket, double timeout);
double medusa_udpsocket_get_read_timeout (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_get_fd (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_events (struct medusa_udpsocket *udpsocket, unsigned int events);
int medusa_udpsocket_add_events (struct medusa_udpsocket *udpsocket, unsigned int events);
int medusa_udpsocket_del_events (struct medusa_udpsocket *udpsocket, unsigned int events);
unsigned int medusa_udpsocket_get_events (const struct medusa_udpsocket *io);

int medusa_udpsocket_get_protocol (struct medusa_udpsocket *udpsocket);
int medusa_udpsocket_get_sockport (struct medusa_udpsocket *udpsocket);
int medusa_udpsocket_get_sockname (struct medusa_udpsocket *udpsocket, struct sockaddr_storage *sockaddr);
int medusa_udpsocket_get_peername (struct medusa_udpsocket *udpsocket, struct sockaddr_storage *sockaddr);

int medusa_udpsocket_set_context (struct medusa_udpsocket *udpsocket, void *context);
void * medusa_udpsocket_get_context (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_userdata (struct medusa_udpsocket *udpsocket, void *userdata);
void * medusa_udpsocket_get_userdata (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_userdata_ptr (struct medusa_udpsocket *udpsocket, void *userdata);
void * medusa_udpsocket_get_userdata_ptr (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_userdata_int (struct medusa_udpsocket *udpsocket, int userdara);
int medusa_udpsocket_get_userdata_int (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_userdata_uint (struct medusa_udpsocket *udpsocket, unsigned int userdata);
unsigned int medusa_udpsocket_get_userdata_uint (struct medusa_udpsocket *udpsocket);

struct medusa_monitor * medusa_udpsocket_get_monitor (struct medusa_udpsocket *udpsocket);

const char * medusa_udpsocket_protocol_string (unsigned int protocol);
const char * medusa_udpsocket_state_string (unsigned int state);
const char * medusa_udpsocket_event_string (unsigned int events);

#ifdef __cplusplus
}
#endif

#endif
