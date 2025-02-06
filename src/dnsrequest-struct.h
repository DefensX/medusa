
#if !defined(MEDUSA_DNSREQUEST_STRUCT_H)
#define MEDUSA_DNSREQUEST_STRUCT_H

struct medusa_dnsrequest {
        struct medusa_subject subject;
        unsigned int state;
        unsigned int error;
        int (*onevent) (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param);
        void *context;
        double resolve_timeout;
        double connect_timeout;
        double receive_timeout;
        char *nameserver;
        int port;
        unsigned int code;
        unsigned int type;
        char *name;
        int id;
        void *userdata;
        struct medusa_udpsocket *udpsocket;
        struct medusa_dnsrequest_reply *reply;
};

int medusa_dnsrequest_init (struct medusa_dnsrequest *dnsrequest, struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param), void *context);
int medusa_dnsrequest_init_with_options (struct medusa_dnsrequest *dnsrequest, const struct medusa_dnsrequest_init_options *options);
void medusa_dnsrequest_uninit (struct medusa_dnsrequest *dnsrequest);

#endif
