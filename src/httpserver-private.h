
#if !defined(MEDUSA_HTTPSERVER_PRIVATE_H)
#define MEDUSA_HTTPSERVER_PRIVATE_H

struct medusa_httpserver;
struct medusa_httpserver_client;

struct medusa_httpserver * medusa_httpserver_create_unlocked (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_httpserver *httpserver, unsigned int events, void *context, void *param), void *context);
struct medusa_httpserver * medusa_httpserver_create_with_options_unlocked (const struct medusa_httpserver_init_options *options);

void medusa_httpserver_destroy_unlocked (struct medusa_httpserver *httpserver);

int medusa_httpserver_get_state_unlocked (const struct medusa_httpserver *httpserver);
int medusa_httpserver_get_error_unlocked (const struct medusa_httpserver *httpserver);

int medusa_httpserver_get_protocol_unlocked (struct medusa_httpserver *httpserver);
int medusa_httpserver_get_sockport_unlocked (const struct medusa_httpserver *httpserver);
int medusa_httpserver_get_sockname_unlocked (const struct medusa_httpserver *httpserver, struct sockaddr_storage *sockaddr);

int medusa_httpserver_set_enabled_unlocked (struct medusa_httpserver *httpserver, int enabled);
int medusa_httpserver_get_enabled_unlocked (const struct medusa_httpserver *httpserver);

int medusa_httpserver_pause_unlocked (struct medusa_httpserver *httpserver);
int medusa_httpserver_resume_unlocked (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_started_unlocked (struct medusa_httpserver *httpserver, int started);
int medusa_httpserver_get_started_unlocked (const struct medusa_httpserver *httpserver);

int medusa_httpserver_start_unlocked (struct medusa_httpserver *httpserver);
int medusa_httpserver_stop_unlocked (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_ssl_unlocked (struct medusa_httpserver *httpserver, int enable);
int medusa_httpserver_get_ssl_unlocked (const struct medusa_httpserver *httpserver);

int medusa_httpserver_set_ssl_certificate_unlocked (struct medusa_httpserver *httpserver, const char *certificate, int length);
int medusa_httpserver_set_ssl_certificate_file_unlocked (struct medusa_httpserver *httpserver, const char *certificate);
const char * medusa_httpserver_get_ssl_certificate_unlocked (const struct medusa_httpserver *httpserver);

int medusa_httpserver_set_ssl_privatekey_unlocked (struct medusa_httpserver *httpserver, const char *privatekey, int length);
int medusa_httpserver_set_ssl_privatekey_file_unlocked (struct medusa_httpserver *httpserver, const char *privatekey);
const char * medusa_httpserver_get_ssl_privatekey_unlocked (const struct medusa_httpserver *httpserver);

int medusa_httpserver_set_context_unlocked (struct medusa_httpserver *httpserver, void *context);
void * medusa_httpserver_get_context_unlocked (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_userdata_unlocked (struct medusa_httpserver *httpserver, void *userdata);
void * medusa_httpserver_get_userdata_unlocked (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_userdata_ptr_unlocked (struct medusa_httpserver *httpserver, void *userdata);
void * medusa_httpserver_get_userdata_ptr_unlocked (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_userdata_int_unlocked (struct medusa_httpserver *httpserver, int userdara);
int medusa_httpserver_get_userdata_int_unlocked (struct medusa_httpserver *httpserver);

int medusa_httpserver_set_userdata_uint_unlocked (struct medusa_httpserver *httpserver, unsigned int userdata);
unsigned int medusa_httpserver_get_userdata_uint_unlocked (struct medusa_httpserver *httpserver);

int medusa_httpserver_onevent_unlocked (struct medusa_httpserver *httpserver, unsigned int events, void *param);
struct medusa_monitor * medusa_httpserver_get_monitor_unlocked (struct medusa_httpserver *httpserver);

struct medusa_httpserver_client * medusa_httpserver_accept_unlocked (struct medusa_httpserver *httpserver, int (*onevent) (struct medusa_httpserver_client *httpserver_client, unsigned int events, void *context, void *param), void *context);
struct medusa_httpserver_client * medusa_httpserver_accept_with_options_unlocked (struct medusa_httpserver *httpserver, struct medusa_httpserver_accept_options *options);
void medusa_httpserver_client_destroy_unlocked (struct medusa_httpserver_client *httpserver_client);

unsigned int medusa_httpserver_client_get_state_unlocked (const struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_enabled_unlocked (struct medusa_httpserver_client *httpserver_client, int enabled);
int medusa_httpserver_client_get_enabled_unlocked (const struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_read_timeout_unlocked (struct medusa_httpserver_client *httpserver_client, double timeout);
double medusa_httpserver_client_get_read_timeout_unlocked (const struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_write_timeout_unlocked (struct medusa_httpserver_client *httpserver_client, double timeout);
double medusa_httpserver_client_get_write_timeout_unlocked (const struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_reply_send_start_unlocked (struct medusa_httpserver_client *httpserver_client);
int medusa_httpserver_client_reply_send_status_unlocked (struct medusa_httpserver_client *httpserver_client, const char *version, int code, const char *reason);
int medusa_httpserver_client_reply_send_statusf_unlocked (struct medusa_httpserver_client *httpserver_client, const char *version, int code, const char *reason, ...) __attribute__((format(printf, 4, 5)));
int medusa_httpserver_client_reply_send_statusv_unlocked (struct medusa_httpserver_client *httpserver_client, const char *version, int code, const char *reason, va_list va);
int medusa_httpserver_client_reply_send_header_unlocked (struct medusa_httpserver_client *httpserver_client, const char *key, const char *value);
int medusa_httpserver_client_reply_send_headerf_unlocked (struct medusa_httpserver_client *httpserver_client, const char *key, const char *value, ...) __attribute__((format(printf, 3, 4)));
int medusa_httpserver_client_reply_send_headerv_unlocked (struct medusa_httpserver_client *httpserver_client, const char *key, const char *value, va_list va);
int medusa_httpserver_client_reply_send_body_unlocked (struct medusa_httpserver_client *httpserver_client, const void *body, int length);
int medusa_httpserver_client_reply_send_bodyf_unlocked (struct medusa_httpserver_client *httpserver_client, const char *body, ...) __attribute__((format(printf, 2, 3)));
int medusa_httpserver_client_reply_send_bodyv_unlocked (struct medusa_httpserver_client *httpserver_client, const char *body, va_list va);
int medusa_httpserver_client_reply_send_finish_unlocked (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_get_fd_unlocked (struct medusa_httpserver_client *httpserver_client);
int medusa_httpserver_client_get_sockname_unlocked (struct medusa_httpserver_client *httpserver_client, struct sockaddr_storage *sockaddr);
int medusa_httpserver_client_get_peername_unlocked (struct medusa_httpserver_client *httpserver_client, struct sockaddr_storage *sockaddr);

int medusa_httpserver_client_set_context_unlocked (struct medusa_httpserver_client *httpserver_client, void *context);
void * medusa_httpserver_client_get_context_unlocked (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_userdata_unlocked (struct medusa_httpserver_client *httpserver_client, void *userdata);
void * medusa_httpserver_client_get_userdata_unlocked (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_userdata_ptr_unlocked (struct medusa_httpserver_client *httpserver_client, void *userdata);
void * medusa_httpserver_client_get_userdata_ptr_unlocked (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_userdata_int_unlocked (struct medusa_httpserver_client *httpserver_client, int userdara);
int medusa_httpserver_client_get_userdata_int_unlocked (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_set_userdata_uint_unlocked (struct medusa_httpserver_client *httpserver_client, unsigned int userdata);
unsigned int medusa_httpserver_client_get_userdata_uint_unlocked (struct medusa_httpserver_client *httpserver_client);

int medusa_httpserver_client_onevent_unlocked (struct medusa_httpserver_client *httpserver_client, unsigned int events, void *param);
struct medusa_monitor * medusa_httpserver_client_get_monitor_unlocked (struct medusa_httpserver_client *httpserver_client);

#endif
