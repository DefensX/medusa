
#if !defined(MEDUSA_UDPSOCKET_PRIVATE_H)
#define MEDUSA_UDPSOCKET_PRIVATE_H

struct medusa_udpsocket;

struct medusa_udpsocket * medusa_udpsocket_bind_unlocked (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_udpsocket * medusa_udpsocket_bind_with_options_unlocked (const struct medusa_udpsocket_bind_options *options);

struct medusa_udpsocket * medusa_udpsocket_open_unlocked (struct medusa_monitor *monitor, unsigned int protocol, int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_udpsocket * medusa_udpsocket_open_with_options_unlocked (const struct medusa_udpsocket_open_options *options);

struct medusa_udpsocket * medusa_udpsocket_connect_unlocked (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_udpsocket * medusa_udpsocket_connect_with_options_unlocked (const struct medusa_udpsocket_connect_options *options);

struct medusa_udpsocket * medusa_udpsocket_attach_unlocked (struct medusa_monitor *monitor, int fd, int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_udpsocket * medusa_udpsocket_attach_with_options_unlocked (const struct medusa_udpsocket_attach_options *options);

void medusa_udpsocket_destroy_unlocked (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_get_state_unlocked (const struct medusa_udpsocket *udpsocket);
int medusa_udpsocket_get_error_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_enabled_unlocked (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_enabled_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_nonblocking_unlocked (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_nonblocking_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_reuseaddr_unlocked (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_reuseaddr_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_reuseport_unlocked (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_reuseport_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_freebind_unlocked (struct medusa_udpsocket *udpsocket, int enabled);
int medusa_udpsocket_get_freebind_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_read_timeout_unlocked (struct medusa_udpsocket *udpsocket, double timeout);
double medusa_udpsocket_get_read_timeout_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_get_fd_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_events_unlocked (struct medusa_udpsocket *udpsocket, unsigned int events);
int medusa_udpsocket_add_events_unlocked (struct medusa_udpsocket *udpsocket, unsigned int events);
unsigned int medusa_udpsocket_get_events_unlocked (const struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_get_protocol_unlocked (struct medusa_udpsocket *udpsocket);
int medusa_udpsocket_get_sockport_unlocked (struct medusa_udpsocket *udpsocket);
int medusa_udpsocket_get_sockname_unlocked (struct medusa_udpsocket *udpsocket, struct sockaddr_storage *sockaddr);
int medusa_udpsocket_get_peername_unlocked (struct medusa_udpsocket *udpsocket, struct sockaddr_storage *sockaddr);

int medusa_udpsocket_set_context_unlocked (struct medusa_udpsocket *udpsocket, void *context);
void * medusa_udpsocket_get_context_unlocked (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_userdata_unlocked (struct medusa_udpsocket *udpsocket, void *userdata);
void * medusa_udpsocket_get_userdata_unlocked (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_userdata_ptr_unlocked (struct medusa_udpsocket *udpsocket, void *userdata);
void * medusa_udpsocket_get_userdata_ptr_unlocked (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_userdata_int_unlocked (struct medusa_udpsocket *udpsocket, int userdara);
int medusa_udpsocket_get_userdata_int_unlocked (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_set_userdata_uint_unlocked (struct medusa_udpsocket *udpsocket, unsigned int userdata);
unsigned int medusa_udpsocket_get_userdata_uint_unlocked (struct medusa_udpsocket *udpsocket);

int medusa_udpsocket_onevent_unlocked (struct medusa_udpsocket *udpsocket, unsigned int events, void *param);
int medusa_udpsocket_onevent (struct medusa_udpsocket *udpsocket, unsigned int events, void *param);

struct medusa_monitor * medusa_udpsocket_get_monitor_unlocked (struct medusa_udpsocket *udpsocket);

#endif
