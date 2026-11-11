
#if !defined(MEDUSA_IO_STRUCT_H)
#define MEDUSA_IO_STRUCT_H

struct medusa_monitor;
struct medusa_io_init_options;
struct medusa_io;

struct medusa_io {
        struct medusa_subject subject;
        unsigned int flags;
        int fd;
        int (*onevent) (struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
        void *userdata;
        /* owned by the poll backend, NULL when the backend keeps no state */
        void *poll_backend_data;
};

int medusa_io_init (struct medusa_io *io, struct medusa_monitor *monitor, int fd, int (*onevent) (struct medusa_io *io, unsigned int events, void *context, void *param), void *context);
int medusa_io_init_with_options (struct medusa_io *io, const struct medusa_io_init_options *options);
void medusa_io_uninit (struct medusa_io *io);

#endif
