
#if !defined(MEDUSA_DNSRESOLVER_PRIVATE_H)
#define MEDUSA_DNSRESOLVER_PRIVATE_H

struct medusa_dnsresolver;

struct medusa_dnsresolver * medusa_dnsresolver_create_unlocked (struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *context, void *param), void *context);
struct medusa_dnsresolver * medusa_dnsresolver_create_with_options_unlocked (const struct medusa_dnsresolver_init_options *options);
void medusa_dnsresolver_destroy_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_get_state_unlocked (const struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_nameserver_unlocked (struct medusa_dnsresolver *dnsresolver, const char *nameserver);
const char * medusa_dnsresolver_get_nameserver_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_port_unlocked (struct medusa_dnsresolver *dnsresolver, int port);
int medusa_dnsresolver_get_port_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_family_unlocked (struct medusa_dnsresolver *dnsresolver, unsigned int family);
int medusa_dnsresolver_get_family_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_retry_count_unlocked (struct medusa_dnsresolver *dnsresolver, int retry_count);
int medusa_dnsresolver_get_retry_count_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_retry_interval_unlocked (struct medusa_dnsresolver *dnsresolver, double retry_interval);
double medusa_dnsresolver_get_retry_interval_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_resolve_timeout_unlocked (struct medusa_dnsresolver *dnsresolver, double resolve_timeout);
double medusa_dnsresolver_get_resolve_timeout_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_min_ttl_unlocked (struct medusa_dnsresolver *dnsresolver, int min_ttl);
int medusa_dnsresolver_get_min_ttl_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_context_unlocked (struct medusa_dnsresolver *dnsresolver, void *context);
void * medusa_dnsresolver_get_context_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_userdata_unlocked (struct medusa_dnsresolver *dnsresolver, void *userdata);
void * medusa_dnsresolver_get_userdata_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_userdata_ptr_unlocked (struct medusa_dnsresolver *dnsresolver, void *userdata);
void * medusa_dnsresolver_get_userdata_ptr_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_userdata_int_unlocked (struct medusa_dnsresolver *dnsresolver, int userdara);
int medusa_dnsresolver_get_userdata_int_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_userdata_uint_unlocked (struct medusa_dnsresolver *dnsresolver, unsigned int userdata);
unsigned int medusa_dnsresolver_get_userdata_uint_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_enabled_unlocked (struct medusa_dnsresolver *dnsresolver, int enabled);
int medusa_dnsresolver_get_enabled_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_start_unlocked (struct medusa_dnsresolver *dnsresolver);
int medusa_dnsresolver_stop_unlocked (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_onevent_unlocked (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *param);
struct medusa_monitor * medusa_dnsresolver_get_monitor_unlocked (struct medusa_dnsresolver *dnsresolver);

struct medusa_dnsresolver_lookup * medusa_dnsresolver_lookup_unlocked (struct medusa_dnsresolver *dnsresolver, unsigned int family, const char *name, int (*onevent) (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *context, void *param), void *context);
struct medusa_dnsresolver_lookup * medusa_dnsresolver_lookup_with_options_unlocked (struct medusa_dnsresolver *dnsresolver, const struct medusa_dnsresolver_lookup_options *options);
void medusa_dnsresolver_lookup_destroy_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_get_state_unlocked (const struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_nameserver_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, const char *nameserver);
const char * medusa_dnsresolver_lookup_get_nameserver_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_port_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int port);
int medusa_dnsresolver_lookup_get_port_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_family_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int family);
int medusa_dnsresolver_lookup_get_family_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_name_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, const char *name);
const char * medusa_dnsresolver_lookup_get_name_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_id_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int id);
int medusa_dnsresolver_lookup_get_id_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_retry_count_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int retry_count);
int medusa_dnsresolver_lookup_get_retry_count_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_retry_interval_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, double retry_interval);
double medusa_dnsresolver_lookup_get_retry_interval_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_resolve_timeout_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, double resolve_timeout);
double medusa_dnsresolver_lookup_get_resolve_timeout_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_context_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *context);
void * medusa_dnsresolver_lookup_get_context_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_userdata_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *userdata);
void * medusa_dnsresolver_lookup_get_userdata_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_userdata_ptr_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *userdata);
void * medusa_dnsresolver_lookup_get_userdata_ptr_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_userdata_int_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int userdara);
int medusa_dnsresolver_lookup_get_userdata_int_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_userdata_uint_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int userdata);
unsigned int medusa_dnsresolver_lookup_get_userdata_uint_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_enabled_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int enabled);
int medusa_dnsresolver_lookup_get_enabled_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_start_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);
int medusa_dnsresolver_lookup_stop_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_onevent_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *param);
struct medusa_monitor * medusa_dnsresolver_lookup_get_monitor_unlocked (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

#endif
