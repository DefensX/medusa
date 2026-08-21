
#if !defined(MEDUSA_IO_H)
#define MEDUSA_IO_H

struct medusa_io;
struct medusa_monitor;

enum {
        MEDUSA_IO_EVENT_NONE    = 0x00000000,
        MEDUSA_IO_EVENT_IN      = 0x00000001,
        MEDUSA_IO_EVENT_OUT     = 0x00000002,
        MEDUSA_IO_EVENT_PRI     = 0x00000004,
        MEDUSA_IO_EVENT_ERR     = 0x00000008,
        MEDUSA_IO_EVENT_HUP     = 0x00000010,
        MEDUSA_IO_EVENT_EXCP    = (MEDUSA_IO_EVENT_ERR | MEDUSA_IO_EVENT_HUP),
        MEDUSA_IO_EVENT_NVAL    = 0x00000020,
        MEDUSA_IO_EVENT_DESTROY = 0x00000040
#define MEDUSA_IO_EVENT_NONE    MEDUSA_IO_EVENT_NONE
#define MEDUSA_IO_EVENT_IN      MEDUSA_IO_EVENT_IN
#define MEDUSA_IO_EVENT_OUT     MEDUSA_IO_EVENT_OUT
#define MEDUSA_IO_EVENT_PRI     MEDUSA_IO_EVENT_PRI
#define MEDUSA_IO_EVENT_ERR     MEDUSA_IO_EVENT_ERR
#define MEDUSA_IO_EVENT_HUP     MEDUSA_IO_EVENT_HUP
#define MEDUSA_IO_EVENT_NVAL    MEDUSA_IO_EVENT_NVAL
#define MEDUSA_IO_EVENT_DESTROY MEDUSA_IO_EVENT_DESTROY
};

struct medusa_io_init_options {
        struct medusa_monitor *monitor;
        int fd;
        int (*onevent) (struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
        unsigned int events;
        int clodestroy;
        int enabled;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_io_init_options_default (struct medusa_io_init_options *options);

struct medusa_io * medusa_io_create (struct medusa_monitor *monitor, int fd, int (*onevent) (struct medusa_io *io, unsigned int events, void *context, void *param), void *context);
struct medusa_io * medusa_io_create_with_options (const struct medusa_io_init_options *options);
void medusa_io_destroy (struct medusa_io *io);

int medusa_io_get_fd (const struct medusa_io *io);

int medusa_io_set_events (struct medusa_io *io, unsigned int events);
int medusa_io_add_events (struct medusa_io *io, unsigned int events);
int medusa_io_del_events (struct medusa_io *io, unsigned int events);
unsigned int medusa_io_get_events (const struct medusa_io *io);

int medusa_io_set_enabled (struct medusa_io *io, int enabled);
int medusa_io_get_enabled (const struct medusa_io *io);

int medusa_io_set_clodestroy (struct medusa_io *io, int clodestroy);
int medusa_io_get_clodestroy (const struct medusa_io *io);

int medusa_io_enable (struct medusa_io *io);
int medusa_io_disable (struct medusa_io *io);

int medusa_io_set_context (struct medusa_io *io, void *context);
void * medusa_io_get_context (struct medusa_io *io);

int medusa_io_set_userdata (struct medusa_io *io, void *userdata);
void * medusa_io_get_userdata (struct medusa_io *io);

int medusa_io_set_userdata_ptr (struct medusa_io *io, void *userdata);
void * medusa_io_get_userdata_ptr (struct medusa_io *io);

int medusa_io_set_userdata_int (struct medusa_io *io, int userdara);
int medusa_io_get_userdata_int (struct medusa_io *io);

int medusa_io_set_userdata_uint (struct medusa_io *io, unsigned int userdata);
unsigned int medusa_io_get_userdata_uint (struct medusa_io *io);

struct medusa_monitor * medusa_io_get_monitor (const struct medusa_io *io);

const char * medusa_io_event_string (unsigned int events);
const char * medusa_io_events_string (unsigned int events);

#ifdef __cplusplus
}
#endif

#endif
