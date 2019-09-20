
#if !defined(MEDUSA_POLL_EPOLL_H)
#define MEDUSA_POLL_EPOLL_H

struct medusa_poll_backend;

struct medusa_monitor_epoll_init_options {
        int (*onevent) (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
};

struct medusa_poll_backend * medusa_monitor_epoll_create (const struct medusa_monitor_epoll_init_options *options);

#endif
