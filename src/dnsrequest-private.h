
#if !defined(MEDUSA_DNSREQUEST_PRIVATE_H)
#define MEDUSA_DNSREQUEST_PRIVATE_H

struct medusa_dnsrequest;

struct medusa_dnsrequest * medusa_dnsrequest_create_lookup_unlocked (struct medusa_monitor *monitor, const char *nameserver, unsigned int type, const char *name, int (*onevent) (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param), void *context);

struct medusa_dnsrequest * medusa_dnsrequest_create_unlocked (struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param), void *context);
struct medusa_dnsrequest * medusa_dnsrequest_create_with_options_unlocked (const struct medusa_dnsrequest_init_options *options);
void medusa_dnsrequest_destroy_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_get_state_unlocked (const struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_resolve_timeout_unlocked (struct medusa_dnsrequest *dnsrequest, double timeout);
double medusa_dnsrequest_get_resolve_timeout_unlocked (const struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_connect_timeout_unlocked (struct medusa_dnsrequest *dnsrequest, double timeout);
double medusa_dnsrequest_get_connect_timeout_unlocked (const struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_receive_timeout_unlocked (struct medusa_dnsrequest *dnsrequest, double timeout);
double medusa_dnsrequest_get_receive_timeout_unlocked (const struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_nameserver_unlocked (struct medusa_dnsrequest *dnsrequest, const char *nameserver);
const char * medusa_dnsrequest_get_nameserver_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_port_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int port);
int medusa_dnsrequest_get_port_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_code_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int code);
int medusa_dnsrequest_get_code_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_type_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int type);
int medusa_dnsrequest_get_type_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_name_unlocked (struct medusa_dnsrequest *dnsrequest, const char *name);
const char * medusa_dnsrequest_get_name_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_id_unlocked (struct medusa_dnsrequest *dnsrequest, int id);
int medusa_dnsrequest_get_id_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_context_unlocked (struct medusa_dnsrequest *dnsrequest, void *context);
void * medusa_dnsrequest_get_context_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_userdata_unlocked (struct medusa_dnsrequest *dnsrequest, void *userdata);
void * medusa_dnsrequest_get_userdata_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_userdata_ptr_unlocked (struct medusa_dnsrequest *dnsrequest, void *userdata);
void * medusa_dnsrequest_get_userdata_ptr_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_userdata_int_unlocked (struct medusa_dnsrequest *dnsrequest, int userdara);
int medusa_dnsrequest_get_userdata_int_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_set_userdata_uint_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int userdata);
unsigned int medusa_dnsrequest_get_userdata_uint_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_lookup_unlocked (struct medusa_dnsrequest *dnsrequest);
int medusa_dnsrequest_cancel_unlocked (struct medusa_dnsrequest *dnsrequest);
int medusa_dnsrequest_abort_unlocked (struct medusa_dnsrequest *dnsrequest);

const struct medusa_dnsrequest_reply * medusa_dnsrequest_get_reply_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_lookup_unlocked (struct medusa_dnsrequest *dnsrequest);

int medusa_dnsrequest_onevent_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *param);
struct medusa_monitor * medusa_dnsrequest_get_monitor_unlocked (struct medusa_dnsrequest *dnsrequest);

#endif
