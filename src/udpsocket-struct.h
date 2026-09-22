
#if !defined(MEDUSA_UDPSOCKET_STRUCT_H)
#define MEDUSA_UDPSOCKET_STRUCT_H

struct medusa_udpsocket {
        struct medusa_subject subject;
        int (*onevent) (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param);
        void *context;
        unsigned int flags;
        unsigned int state;
        unsigned int error;
        struct medusa_io *io;
        struct medusa_udpsocket_connect_options *coptions;
        struct medusa_timer *cdefer;
        struct medusa_dnsresolver_lookup *clookup;
        struct medusa_timer *ltimer;
        struct medusa_timer *ctimer;
        struct medusa_timer *rtimer;
        struct medusa_timer *wtimer;
        struct medusa_buffer *wbuffer;
        struct medusa_buffer *rbuffer;
        int wbuffer_limit;
        int rbuffer_limit;
        void *userdata;
};

#endif
