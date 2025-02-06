
#if !defined(MEDUSA_DNSRESOLVER_STRUCT_H)
#define MEDUSA_DNSRESOLVER_STRUCT_H

TAILQ_HEAD(medusa_dnsresolver_entries, medusa_dnsresolver_entry);
struct medusa_dnsresolver_entry {
        struct timespec then;
        char *name;
        struct medusa_dnsrequest_reply_answers *answers;
        TAILQ_ENTRY(medusa_dnsresolver_entry) tailq;
};

TAILQ_HEAD(medusa_dnsresolver_lookups, medusa_dnsresolver_lookup);
struct medusa_dnsresolver_lookup {
        struct medusa_subject subject;
        unsigned int state;
        unsigned int error;
        unsigned int enabled;
        int (*onevent) (struct medusa_dnsresolver_lookup *dnsresolver_lookup, unsigned int events, void *context, void *param);
        void *context;
        char *nameserver;
        int port;
        char *name;
        int id;
        unsigned int family;
        int retry_count;
        double retry_interval;
        double resolve_timeout;
        void *userdata;
        int retried_count;
        struct medusa_timer *retry_interval_timer;
        struct medusa_timer *resolve_timeout_timer;
        struct medusa_dnsrequest *dnsrequest;
        TAILQ_ENTRY(medusa_dnsresolver_lookup) tailq;
        struct medusa_dnsresolver *dnsresolver;
};

struct medusa_dnsresolver {
        struct medusa_subject subject;
        unsigned int state;
        unsigned int error;
        unsigned int enabled;
        int (*onevent) (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *context, void *param);
        void *context;
        char *nameserver;
        int port;
        unsigned int family;
        int retry_count;
        double retry_interval;
        double resolve_timeout;
        int min_ttl;
        void *userdata;
        struct medusa_dnsresolver_lookups lookups;
        struct medusa_dnsresolver_entries entries;
};

int medusa_dnsresolver_init (struct medusa_dnsresolver *dnsresolver, struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsresolver *dnsresolver, unsigned int events, void *context, void *param), void *context);
int medusa_dnsresolver_init_with_options (struct medusa_dnsresolver *dnsresolver, const struct medusa_dnsresolver_init_options *options);
void medusa_dnsresolver_uninit (struct medusa_dnsresolver *dnsresolver);

#endif
