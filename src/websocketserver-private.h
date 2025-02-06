
#if !defined(MEDUSA_WEBSOCKETSERVER_PRIVATE_H)
#define MEDUSA_WEBSOCKETSERVER_PRIVATE_H

struct medusa_websocketserver;
struct medusa_websocketserver_client;

struct medusa_websocketserver * medusa_websocketserver_create_unlocked (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_websocketserver *websocketserver, unsigned int events, void *context, void *param), void *context);
struct medusa_websocketserver * medusa_websocketserver_create_with_options_unlocked (const struct medusa_websocketserver_init_options *options);

void medusa_websocketserver_destroy_unlocked (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_get_state_unlocked (const struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_get_error_unlocked (const struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_get_protocol_unlocked (struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_get_sockport_unlocked (const struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_get_sockname_unlocked (const struct medusa_websocketserver *websocketserver, struct sockaddr_storage *sockaddr);

int medusa_websocketserver_set_enabled_unlocked (struct medusa_websocketserver *websocketserver, int enabled);
int medusa_websocketserver_get_enabled_unlocked (const struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_pause_unlocked (struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_resume_unlocked (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_started_unlocked (struct medusa_websocketserver *websocketserver, int started);
int medusa_websocketserver_get_started_unlocked (const struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_start_unlocked (struct medusa_websocketserver *websocketserver);
int medusa_websocketserver_stop_unlocked (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_context_unlocked (struct medusa_websocketserver *websocketserver, void *context);
void * medusa_websocketserver_get_context_unlocked (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_userdata_unlocked (struct medusa_websocketserver *websocketserver, void *userdata);
void * medusa_websocketserver_get_userdata_unlocked (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_userdata_ptr_unlocked (struct medusa_websocketserver *websocketserver, void *userdata);
void * medusa_websocketserver_get_userdata_ptr_unlocked (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_userdata_int_unlocked (struct medusa_websocketserver *websocketserver, int userdara);
int medusa_websocketserver_get_userdata_int_unlocked (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_set_userdata_uint_unlocked (struct medusa_websocketserver *websocketserver, unsigned int userdata);
unsigned int medusa_websocketserver_get_userdata_uint_unlocked (struct medusa_websocketserver *websocketserver);

int medusa_websocketserver_onevent_unlocked (struct medusa_websocketserver *websocketserver, unsigned int events, void *param);
struct medusa_monitor * medusa_websocketserver_get_monitor_unlocked (struct medusa_websocketserver *websocketserver);

struct medusa_websocketserver_client * medusa_websocketserver_accept_unlocked (struct medusa_websocketserver *websocketserver, int (*onevent) (struct medusa_websocketserver_client *websocketserver_client, unsigned int events, void *context, void *param), void *context);
struct medusa_websocketserver_client * medusa_websocketserver_accept_with_options_unlocked (struct medusa_websocketserver *websocketserver, struct medusa_websocketserver_accept_options *options);
void medusa_websocketserver_client_destroy_unlocked (struct medusa_websocketserver_client *websocketserver_client);

unsigned int medusa_websocketserver_client_get_state_unlocked (const struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_enabled_unlocked (struct medusa_websocketserver_client *websocketserver_client, int enabled);
int medusa_websocketserver_client_get_enabled_unlocked (const struct medusa_websocketserver_client *websocketserver_client);

struct medusa_buffer * medusa_websocketserver_client_get_read_buffer_unlocked (const struct medusa_websocketserver_client *websocketserver_client);
struct medusa_buffer * medusa_websocketserver_client_get_write_buffer_unlocked (const struct medusa_websocketserver_client *websocketserver_client);

int64_t medusa_websocketserver_client_write_unlocked (struct medusa_websocketserver_client *websocketserver_client, unsigned int final, unsigned int type, const void *data, int64_t length);

int medusa_websocketserver_client_get_sockname_unlocked (struct medusa_websocketserver_client *websocketserver_client, struct sockaddr_storage *sockaddr);
int medusa_websocketserver_client_get_peername_unlocked (struct medusa_websocketserver_client *websocketserver_client, struct sockaddr_storage *sockaddr);

int medusa_websocketserver_client_set_context_unlocked (struct medusa_websocketserver_client *websocketserver_client, void *context);
void * medusa_websocketserver_client_get_context_unlocked (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_userdata_unlocked (struct medusa_websocketserver_client *websocketserver_client, void *userdata);
void * medusa_websocketserver_client_get_userdata_unlocked (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_userdata_ptr_unlocked (struct medusa_websocketserver_client *websocketserver_client, void *userdata);
void * medusa_websocketserver_client_get_userdata_ptr_unlocked (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_userdata_int_unlocked (struct medusa_websocketserver_client *websocketserver_client, int userdara);
int medusa_websocketserver_client_get_userdata_int_unlocked (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_set_userdata_uint_unlocked (struct medusa_websocketserver_client *websocketserver_client, unsigned int userdata);
unsigned int medusa_websocketserver_client_get_userdata_uint_unlocked (struct medusa_websocketserver_client *websocketserver_client);

int medusa_websocketserver_client_onevent_unlocked (struct medusa_websocketserver_client *websocketserver_client, unsigned int events, void *param);
struct medusa_monitor * medusa_websocketserver_client_get_monitor_unlocked (struct medusa_websocketserver_client *websocketserver_client);

#endif
