
#if !defined(MEDUSA_DNSRESOLVER_H)
#define MEDUSA_DNSRESOLVER_H

struct medusa_monitor;
struct medusa_dnsresolver;
struct medusa_dnsresolver_lookup;

enum {
        MEDUSA_DNSRESOLVER_FAMILY_ANY                   = 0,
        MEDUSA_DNSRESOLVER_FAMILY_IPV4                  = 1,
        MEDUSA_DNSRESOLVER_FAMILY_IPV6                  = 2
#define MEDUSA_DNSRESOLVER_FAMILY_ANY                   MEDUSA_DNSRESOLVER_FAMILY_ANY
#define MEDUSA_DNSRESOLVER_FAMILY_IPV4                  MEDUSA_DNSRESOLVER_FAMILY_IPV4
#define MEDUSA_DNSRESOLVER_FAMILY_IPV6                  MEDUSA_DNSRESOLVER_FAMILY_IPV6
};

enum {
        MEDUSA_DNSRESOLVER_EVENT_STARTED                = (1 << 0),
        MEDUSA_DNSRESOLVER_EVENT_STOPPED                = (1 << 1),
        MEDUSA_DNSRESOLVER_EVENT_ERROR                  = (1 << 2),
        MEDUSA_DNSRESOLVER_EVENT_STATE_CHANGED          = (1 << 3),
        MEDUSA_DNSRESOLVER_EVENT_DESTROY                = (1 << 4)
#define MEDUSA_DNSRESOLVER_EVENT_STARTED                MEDUSA_DNSRESOLVER_EVENT_STARTED
#define MEDUSA_DNSRESOLVER_EVENT_STOPPED                MEDUSA_DNSRESOLVER_EVENT_STOPPED
#define MEDUSA_DNSRESOLVER_EVENT_ERROR                  MEDUSA_DNSRESOLVER_EVENT_ERROR
#define MEDUSA_DNSRESOLVER_EVENT_STATE_CHANGED          MEDUSA_DNSRESOLVER_EVENT_STATE_CHANGED
#define MEDUSA_DNSRESOLVER_EVENT_DESTROY                MEDUSA_DNSRESOLVER_EVENT_DESTROY
};

enum {
        MEDUSA_DNSRESOLVER_STATE_UNKNOWN                = 0,
        MEDUSA_DNSRESOLVER_STATE_STARTED                = 1,
        MEDUSA_DNSRESOLVER_STATE_STOPPED                = 2,
        MEDUSA_DNSRESOLVER_STATE_ERROR                  = 3
#define MEDUSA_DNSRESOLVER_STATE_UNKNOWN                MEDUSA_DNSRESOLVER_STATE_UNKNOWN
#define MEDUSA_DNSRESOLVER_STATE_STARTED                MEDUSA_DNSRESOLVER_STATE_STARTED
#define MEDUSA_DNSRESOLVER_STATE_STOPPED                MEDUSA_DNSRESOLVER_STATE_STOPPED
#define MEDUSA_DNSRESOLVER_STATE_ERROR                  MEDUSA_DNSRESOLVER_STATE_ERROR
};

enum {
        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STARTED         = (1 << 0),
        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STOPPED         = (1 << 1),
        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY           = (1 << 2),
        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_FINISHED        = (1 << 3),
        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_TIMEDOUT        = (1 << 4),
        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ERROR           = (1 << 5),
        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STATE_CHANGED   = (1 << 6),
        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY         = (1 << 7)
#define MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STARTED         MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STARTED
#define MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STOPPED         MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STOPPED
#define MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY           MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ENTRY
#define MEDUSA_DNSRESOLVER_LOOKUP_EVENT_FINISHED        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_FINISHED
#define MEDUSA_DNSRESOLVER_LOOKUP_EVENT_TIMEDOUT        MEDUSA_DNSRESOLVER_LOOKUP_EVENT_TIMEDOUT
#define MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ERROR           MEDUSA_DNSRESOLVER_LOOKUP_EVENT_ERROR
#define MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STATE_CHANGED   MEDUSA_DNSRESOLVER_LOOKUP_EVENT_STATE_CHANGED
#define MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY         MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY
};

enum {
        MEDUSA_DNSRESOLVER_LOOKUP_STATE_UNKNOWN         = 0,
        MEDUSA_DNSRESOLVER_LOOKUP_STATE_STARTED         = 1,
        MEDUSA_DNSRESOLVER_LOOKUP_STATE_STOPPED         = 2,
        MEDUSA_DNSRESOLVER_LOOKUP_STATE_FINISHED        = 3,
        MEDUSA_DNSRESOLVER_LOOKUP_STATE_TIMEDOUT        = 4,
        MEDUSA_DNSRESOLVER_LOOKUP_STATE_ERROR           = 5
#define MEDUSA_DNSRESOLVER_LOOKUP_STATE_UNKNOWN         MEDUSA_DNSRESOLVER_LOOKUP_STATE_UNKNOWN
#define MEDUSA_DNSRESOLVER_LOOKUP_STATE_STARTED         MEDUSA_DNSRESOLVER_LOOKUP_STATE_STARTED
#define MEDUSA_DNSRESOLVER_LOOKUP_STATE_STOPPED         MEDUSA_DNSRESOLVER_LOOKUP_STATE_STOPPED
#define MEDUSA_DNSRESOLVER_LOOKUP_STATE_FINISHED        MEDUSA_DNSRESOLVER_LOOKUP_STATE_FINISHED
#define MEDUSA_DNSRESOLVER_LOOKUP_STATE_TIMEDOUT        MEDUSA_DNSRESOLVER_LOOKUP_STATE_TIMEDOUT
#define MEDUSA_DNSRESOLVER_LOOKUP_STATE_ERROR           MEDUSA_DNSRESOLVER_LOOKUP_STATE_ERROR
};

struct medusa_dnsresolver_init_options {
        struct medusa_monitor *monitor;
        int (*onevent) (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *context, void *param);
        void *context;
        const char *nameserver;
        int port;
        unsigned int family;
        int retry_count;
        double retry_interval;
        double resolve_timeout;
        int min_ttl;
        int enabled;
};

struct medusa_dnsresolver_event_error {
        unsigned int state;
        unsigned int error;
};

struct medusa_dnsresolver_event_state_changed {
        unsigned int pstate;
        unsigned int state;
        unsigned int error;
};

struct medusa_dnsresolver_lookup_options {
        int (*onevent) (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *context, void *param);
        void *context;
        const char *nameserver;
        int port;
        const char *name;
        int id;
        unsigned int family;
        int retry_count;
        double retry_interval;
        double resolve_timeout;
        int enabled;
};

struct medusa_dnsresolver_lookup_event_entry {
        unsigned int family;
        const char *addreess;
        int ttl;
};

struct medusa_dnsresolver_lookup_event_error {
        unsigned int state;
        unsigned int error;
};

struct medusa_dnsresolver_lookup_event_state_changed {
        unsigned int pstate;
        unsigned int state;
        unsigned int error;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_dnsresolver_init_options_default (struct medusa_dnsresolver_init_options *options);

struct medusa_dnsresolver * medusa_dnsresolver_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *context, void *param), void *context);
struct medusa_dnsresolver * medusa_dnsresolver_create_with_options (const struct medusa_dnsresolver_init_options *options);
void medusa_dnsresolver_destroy (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_get_state (const struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_nameserver (struct medusa_dnsresolver *dnsresolver, const char *nameserver);
const char * medusa_dnsresolver_get_nameserver (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_port (struct medusa_dnsresolver *dnsresolver, int port);
int medusa_dnsresolver_get_port (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_family (struct medusa_dnsresolver *dnsresolver, unsigned int family);
int medusa_dnsresolver_get_family (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_retry_count (struct medusa_dnsresolver *dnsresolver, int retry_count);
int medusa_dnsresolver_get_retry_count (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_retry_interval (struct medusa_dnsresolver *dnsresolver, double retry_interval);
double medusa_dnsresolver_get_retry_interval (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_resolve_timeout (struct medusa_dnsresolver *dnsresolver, double resolve_timeout);
double medusa_dnsresolver_get_resolve_timeout (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_min_ttl (struct medusa_dnsresolver *dnsresolver, int min_ttl);
int medusa_dnsresolver_get_min_ttl (struct medusa_dnsresolver *dnsresolver);

void * medusa_dnsresolver_get_context (struct medusa_dnsresolver *dnsresolver);
int medusa_dnsresolver_set_context (struct medusa_dnsresolver *dnsresolver, void *context);

void * medusa_dnsresolver_get_userdata (struct medusa_dnsresolver *dnsresolver);
int medusa_dnsresolver_set_userdata (struct medusa_dnsresolver *dnsresolver, void *userdata);

int medusa_dnsresolver_set_userdata_ptr (struct medusa_dnsresolver *dnsresolver, void *userdata);
void * medusa_dnsresolver_get_userdata_ptr (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_userdata_int (struct medusa_dnsresolver *dnsresolver, int userdara);
int medusa_dnsresolver_get_userdata_int (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_userdata_uint (struct medusa_dnsresolver *dnsresolver, unsigned int userdata);
unsigned int medusa_dnsresolver_get_userdata_uint (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_set_enabled (struct medusa_dnsresolver *dnsresolver, int enabled);
int medusa_dnsresolver_get_enabled (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_start (struct medusa_dnsresolver *dnsresolver);
int medusa_dnsresolver_stop (struct medusa_dnsresolver *dnsresolver);

int medusa_dnsresolver_onevent (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *param);
struct medusa_monitor * medusa_dnsresolver_get_monitor (struct medusa_dnsresolver *dnsresolver);

const char * medusa_dnsresolver_event_string (unsigned int events);
const char * medusa_dnsresolver_state_string (unsigned int state);

int medusa_dnsresolver_lookup_options_default (struct medusa_dnsresolver_lookup_options *options);

struct medusa_dnsresolver_lookup * medusa_dnsresolver_lookup (struct medusa_dnsresolver *dnsresolver, unsigned int family, const char *name, int (*onevent) (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *context, void *param), void *context);
struct medusa_dnsresolver_lookup * medusa_dnsresolver_lookup_with_options (struct medusa_dnsresolver *dnsresolver, const struct medusa_dnsresolver_lookup_options *options);
void medusa_dnsresolver_lookup_destroy (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_get_state (const struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_nameserver (struct medusa_dnsresolver_lookup *dnsresolver_lookup, const char *nameserver);
const char * medusa_dnsresolver_lookup_get_nameserver (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_port (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int port);
int medusa_dnsresolver_lookup_get_port (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_family (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int family);
int medusa_dnsresolver_lookup_get_family (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_name (struct medusa_dnsresolver_lookup *dnsresolver_lookup, const char *name);
const char * medusa_dnsresolver_lookup_get_name (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_id (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int id);
int medusa_dnsresolver_lookup_get_id (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_retry_count (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int retry_count);
int medusa_dnsresolver_lookup_get_retry_count (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_retry_interval (struct medusa_dnsresolver_lookup *dnsresolver_lookup, double retry_interval);
double medusa_dnsresolver_lookup_get_retry_interval (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_resolve_timeout (struct medusa_dnsresolver_lookup *dnsresolver_lookup, double resolve_timeout);
double medusa_dnsresolver_lookup_get_resolve_timeout (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

void * medusa_dnsresolver_lookup_get_context (struct medusa_dnsresolver_lookup *dnsresolver_lookup);
int medusa_dnsresolver_lookup_set_context (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *context);

void * medusa_dnsresolver_lookup_get_userdata (struct medusa_dnsresolver_lookup *dnsresolver_lookup);
int medusa_dnsresolver_lookup_set_userdata (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *userdata);

int medusa_dnsresolver_lookup_set_userdata_ptr (struct medusa_dnsresolver_lookup *dnsresolver_lookup, void *userdata);
void * medusa_dnsresolver_lookup_get_userdata_ptr (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_userdata_int (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int userdara);
int medusa_dnsresolver_lookup_get_userdata_int (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_userdata_uint (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int userdata);
unsigned int medusa_dnsresolver_lookup_get_userdata_uint (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_set_enabled (struct medusa_dnsresolver_lookup *dnsresolver_lookup, int enabled);
int medusa_dnsresolver_lookup_get_enabled (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_start (struct medusa_dnsresolver_lookup *dnsresolver_lookup);
int medusa_dnsresolver_lookup_stop (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

int medusa_dnsresolver_lookup_onevent (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *param);
struct medusa_monitor * medusa_dnsresolver_lookup_get_monitor (struct medusa_dnsresolver_lookup *dnsresolver_lookup);

const char * medusa_dnsresolver_lookup_event_string (unsigned int events);
const char * medusa_dnsresolver_lookup_state_string (unsigned int state);

#ifdef __cplusplus
}
#endif

#endif
