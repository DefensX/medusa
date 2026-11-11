
#if !defined(MEDUSA_POLL_BACKEND_H)
#define MEDUSA_POLL_BACKEND_H

struct timespec;
struct medusa_monitor;
struct medusa_io;
struct medusa_timespec;

struct medusa_poll_backend {
        const char *name;
        int (*add) (struct medusa_poll_backend *backend, struct medusa_io *io);
        int (*mod) (struct medusa_poll_backend *backend, struct medusa_io *io);
        int (*del) (struct medusa_poll_backend *backend, struct medusa_io *io);
        int (*run) (struct medusa_poll_backend *backend, struct timespec *timespec);
        /* optional: wakes a blocked run() without a socket pair. a backend may
         * clear it at runtime, the monitor then falls back to a socket pair */
        int (*wakeup) (struct medusa_poll_backend *backend);
        void (*destroy) (struct medusa_poll_backend *backend);
        struct medusa_monitor *monitor;
};

#endif
