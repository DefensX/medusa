
#if !defined(MEDUSA_TCPSOCKET_PRIVATE_H)
#define MEDUSA_TCPSOCKET_PRIVATE_H

struct medusa_tcpsocket * medusa_tcpsocket_bind_unlocked (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_tcpsocket * medusa_tcpsocket_bind_with_options_unlocked (const struct medusa_tcpsocket_bind_options *options);

struct medusa_tcpsocket * medusa_tcpsocket_accept_unlocked (struct medusa_tcpsocket *tcpsocket, int (*onevent) (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_tcpsocket * medusa_tcpsocket_accept_with_options_unlocked (struct medusa_tcpsocket *tcpsocket, const struct medusa_tcpsocket_accept_options *options);

struct medusa_tcpsocket * medusa_tcpsocket_connect_unlocked (struct medusa_monitor *monitor, unsigned int protocol, const char *address, unsigned short port, int (*onevent) (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_tcpsocket * medusa_tcpsocket_connect_with_options_unlocked (const struct medusa_tcpsocket_connect_options *options);

struct medusa_tcpsocket * medusa_tcpsocket_attach_unlocked (struct medusa_monitor *monitor, int fd, int (*onevent) (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param), void *context);
struct medusa_tcpsocket * medusa_tcpsocket_attach_with_options_unlocked (const struct medusa_tcpsocket_attach_options *options);

void medusa_tcpsocket_destroy_unlocked (struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_get_state_unlocked (const struct medusa_tcpsocket *tcpsocket);
int medusa_tcpsocket_get_error_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_enabled_unlocked (struct medusa_tcpsocket *tcpsocket, int enabled);
int medusa_tcpsocket_get_enabled_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_buffered_unlocked (struct medusa_tcpsocket *tcpsocket, int enabled);
int medusa_tcpsocket_get_buffered_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_buffered_read_limit_unlocked (struct medusa_tcpsocket *tcpsocket, int limit);
int medusa_tcpsocket_get_buffered_read_limit_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_buffered_write_limit_unlocked (struct medusa_tcpsocket *tcpsocket, int limit);
int medusa_tcpsocket_get_buffered_write_limit_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_clodestroy_unlocked (struct medusa_tcpsocket *tcpsocket, int enabled);
int medusa_tcpsocket_get_clodestroy_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_nonblocking_unlocked (struct medusa_tcpsocket *tcpsocket, int enabled);
int medusa_tcpsocket_get_nonblocking_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_nodelay_unlocked (struct medusa_tcpsocket *tcpsocket, int enabled);
int medusa_tcpsocket_get_nodelay_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_reuseaddr_unlocked (struct medusa_tcpsocket *tcpsocket, int enabled);
int medusa_tcpsocket_get_reuseaddr_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_reuseport_unlocked (struct medusa_tcpsocket *tcpsocket, int enabled);
int medusa_tcpsocket_get_reuseport_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_freebind_unlocked (struct medusa_tcpsocket *tcpsocket, int enabled);
int medusa_tcpsocket_get_freebind_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_backlog_unlocked (struct medusa_tcpsocket *tcpsocket, int backlog);
int medusa_tcpsocket_get_backlog_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_resolve_timeout_unlocked (struct medusa_tcpsocket *tcpsocket, double timeout);
double medusa_tcpsocket_get_resolve_timeout_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_connect_timeout_unlocked (struct medusa_tcpsocket *tcpsocket, double timeout);
double medusa_tcpsocket_get_connect_timeout_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_read_timeout_unlocked (struct medusa_tcpsocket *tcpsocket, double timeout);
double medusa_tcpsocket_get_read_timeout_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_write_timeout_unlocked (struct medusa_tcpsocket *tcpsocket, double timeout);
double medusa_tcpsocket_get_write_timeout_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_ssl_unlocked (struct medusa_tcpsocket *tcpsocket, int enable);
int medusa_tcpsocket_get_ssl_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_ssl_verify_unlocked (struct medusa_tcpsocket *tcpsocket, int enable);
int medusa_tcpsocket_get_ssl_verify_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_ssl_hostname_unlocked (struct medusa_tcpsocket *tcpsocket, const char *hostname);
const char * medusa_tcpsocket_get_ssl_hostname_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_ssl_certificate_unlocked (struct medusa_tcpsocket *tcpsocket, const char *certificate, int length);
int medusa_tcpsocket_set_ssl_certificate_file_unlocked (struct medusa_tcpsocket *tcpsocket, const char *certificate);
const char * medusa_tcpsocket_get_ssl_certificate_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_ssl_privatekey_unlocked (struct medusa_tcpsocket *tcpsocket, const char *privatekey, int length);
int medusa_tcpsocket_set_ssl_privatekey_file_unlocked (struct medusa_tcpsocket *tcpsocket, const char *privatekey);
const char * medusa_tcpsocket_get_ssl_privatekey_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_ssl_ca_certificate_unlocked (struct medusa_tcpsocket *tcpsocket, const char *ca_certificate, int length);
int medusa_tcpsocket_set_ssl_ca_certificate_file_unlocked (struct medusa_tcpsocket *tcpsocket, const char *ca_certificate);
const char * medusa_tcpsocket_get_ssl_ca_certificate_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_ssl_set_SSL_unlocked (struct medusa_tcpsocket *tcpsocket, struct ssl_st *ssl);
struct ssl_st * medusa_tcpsocket_ssl_get_SSL_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_ssl_set_SSL_CTX_unlocked (struct medusa_tcpsocket *tcpsocket, struct ssl_ctx_st *ssl_ctx);
struct ssl_ctx_st * medusa_tcpsocket_ssl_get_SSL_CTX_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_get_fd_unlocked (const struct medusa_tcpsocket *tcpsocket);
struct medusa_io * medusa_tcpsocket_get_io_unlocked (const struct medusa_tcpsocket *tcpsocket);
struct medusa_buffer * medusa_tcpsocket_get_read_buffer_unlocked (const struct medusa_tcpsocket *tcpsocket);
struct medusa_buffer * medusa_tcpsocket_get_write_buffer_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_events_unlocked (struct medusa_tcpsocket *tcpsocket, unsigned int events);
int medusa_tcpsocket_add_events_unlocked (struct medusa_tcpsocket *tcpsocket, unsigned int events);
int medusa_tcpsocket_del_events_unlocked (struct medusa_tcpsocket *tcpsocket, unsigned int events);
unsigned int medusa_tcpsocket_get_events_unlocked (const struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_get_protocol_unlocked (struct medusa_tcpsocket *tcpsocket);
int medusa_tcpsocket_get_sockport_unlocked (struct medusa_tcpsocket *tcpsocket);
int medusa_tcpsocket_get_sockname_unlocked (struct medusa_tcpsocket *tcpsocket, struct sockaddr_storage *sockaddr);
int medusa_tcpsocket_get_peername_unlocked (struct medusa_tcpsocket *tcpsocket, struct sockaddr_storage *sockaddr);

int medusa_tcpsocket_set_onevent_unlocked (struct medusa_tcpsocket *tcpsocket, int (*onevent) (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param), void *context);

int medusa_tcpsocket_set_context_unlocked (struct medusa_tcpsocket *tcpsocket, void *context);
void * medusa_tcpsocket_get_context_unlocked (struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_userdata_unlocked (struct medusa_tcpsocket *tcpsocket, void *userdata);
void * medusa_tcpsocket_get_userdata_unlocked (struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_userdata_ptr_unlocked (struct medusa_tcpsocket *tcpsocket, void *userdata);
void * medusa_tcpsocket_get_userdata_ptr_unlocked (struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_userdata_int_unlocked (struct medusa_tcpsocket *tcpsocket, int userdara);
int medusa_tcpsocket_get_userdata_int_unlocked (struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_set_userdata_uint_unlocked (struct medusa_tcpsocket *tcpsocket, unsigned int userdata);
unsigned int medusa_tcpsocket_get_userdata_uint_unlocked (struct medusa_tcpsocket *tcpsocket);

int medusa_tcpsocket_onevent_unlocked (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *param);
int medusa_tcpsocket_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *param);

struct medusa_monitor * medusa_tcpsocket_get_monitor_unlocked (struct medusa_tcpsocket *tcpsocket);

int64_t medusa_tcpsocket_peek_unlocked  (const struct medusa_tcpsocket *tcpsocket, void *data, int64_t length);
int64_t medusa_tcpsocket_read_unlocked  (struct medusa_tcpsocket *tcpsocket, void *data, int64_t length);
int64_t medusa_tcpsocket_write_unlocked (struct medusa_tcpsocket *tcpsocket, const void *data, int64_t length);
int64_t medusa_tcpsocket_writev_unlocked  (struct medusa_tcpsocket *tcpsocket, const struct medusa_iovec *iovecs, int64_t niovecs);
int64_t medusa_tcpsocket_printf_unlocked (struct medusa_tcpsocket *tcpsocket, const char *format, ...)  __attribute__((format(printf, 2, 3)));
int64_t medusa_tcpsocket_vprintf_unlocked (struct medusa_tcpsocket *tcpsocket, const char *format, va_list va);

#endif
