
#if !defined(MEDUSA_WEBSOCKETCLIENT_PRIVATE_H)
#define MEDUSA_WEBSOCKETCLIENT_PRIVATE_H

struct medusa_websocketclient;

struct medusa_websocketclient * medusa_websocketclient_connect_unlocked (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_websocketclient *websocketclient, unsigned int events, void *context, void *param), void *context);
struct medusa_websocketclient * medusa_websocketclient_connect_with_options_unlocked (const struct medusa_websocketclient_connect_options *options);
void medusa_websocketclient_destroy_unlocked (struct medusa_websocketclient *websocketclient);

unsigned int medusa_websocketclient_get_state_unlocked (const struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_enabled_unlocked (struct medusa_websocketclient *websocketclient, int enabled);
int medusa_websocketclient_get_enabled_unlocked (const struct medusa_websocketclient *websocketclient);

struct medusa_buffer * medusa_websocketclient_get_read_buffer_unlocked (const struct medusa_websocketclient *websocketclient);
struct medusa_buffer * medusa_websocketclient_get_write_buffer_unlocked (const struct medusa_websocketclient *websocketclient);

int64_t medusa_websocketclient_write_unlocked (struct medusa_websocketclient *websocketclient, unsigned int final, unsigned int type, const void *data, int64_t length);

int medusa_websocketclient_get_sockname_unlocked (struct medusa_websocketclient *websocketclient, struct sockaddr_storage *sockaddr);
int medusa_websocketclient_get_peername_unlocked (struct medusa_websocketclient *websocketclient, struct sockaddr_storage *sockaddr);

int medusa_websocketclient_set_context_unlocked (struct medusa_websocketclient *websocketclient, void *context);
void * medusa_websocketclient_get_context_unlocked (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_userdata_unlocked (struct medusa_websocketclient *websocketclient, void *userdata);
void * medusa_websocketclient_get_userdata_unlocked (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_userdata_ptr_unlocked (struct medusa_websocketclient *websocketclient, void *userdata);
void * medusa_websocketclient_get_userdata_ptr_unlocked (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_userdata_int_unlocked (struct medusa_websocketclient *websocketclient, int userdara);
int medusa_websocketclient_get_userdata_int_unlocked (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_set_userdata_uint_unlocked (struct medusa_websocketclient *websocketclient, unsigned int userdata);
unsigned int medusa_websocketclient_get_userdata_uint_unlocked (struct medusa_websocketclient *websocketclient);

int medusa_websocketclient_onevent_unlocked (struct medusa_websocketclient *websocketclient, unsigned int events, void *param);
struct medusa_monitor * medusa_websocketclient_get_monitor_unlocked (struct medusa_websocketclient *websocketclient);

#endif
