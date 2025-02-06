
#if !defined(MEDUSA_SIGNAL_H)
#define MEDUSA_SIGNAL_H

enum {
        MEDUSA_SIGNAL_EVENT_FIRED       = 0x00000001,
        MEDUSA_SIGNAL_EVENT_DESTROY     = 0x00000002
#define MEDUSA_SIGNAL_EVENT_TIMEOUT     MEDUSA_SIGNAL_EVENT_TIMEOUT
#define MEDUSA_SIGNAL_EVENT_DESTROY     MEDUSA_SIGNAL_EVENT_DESTROY
};

struct medusa_signal;
struct medusa_monitor;

struct medusa_signal_init_options {
        struct medusa_monitor *monitor;
        int (*onevent) (struct medusa_signal *signal, unsigned int events, void *context, void *param);
        void *context;
        int number;
        int singleshot;
        int enabled;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_signal_init_options_default (struct medusa_signal_init_options *options);

int medusa_signal_create_singleshot (struct medusa_monitor *monitor, int number, int (*onevent) (struct medusa_signal *signal, unsigned int events, void *context, void *param), void *context);

struct medusa_signal * medusa_signal_create (struct medusa_monitor *monitor, int number, int (*onevent) (struct medusa_signal *signal, unsigned int events, void *context, void *param), void *context);
struct medusa_signal * medusa_signal_create_with_options (const struct medusa_signal_init_options *options);
void medusa_signal_destroy (struct medusa_signal *signal);

int medusa_signal_get_number (const struct medusa_signal *signal);

int medusa_signal_set_singleshot (struct medusa_signal *signal, int singleshot);
int medusa_signal_get_singleshot (const struct medusa_signal *signal);

int medusa_signal_set_enabled (struct medusa_signal *signal, int enabled);
int medusa_signal_get_enabled (const struct medusa_signal *signal);

int medusa_signal_enable (struct medusa_signal *signal);
int medusa_signal_disable (struct medusa_signal *signal);

int medusa_signal_set_context (struct medusa_signal *signal, void *context);
void * medusa_signal_get_context (struct medusa_signal *signal);

int medusa_signal_set_userdata (struct medusa_signal *signal, void *userdata);
void * medusa_signal_get_userdata (struct medusa_signal *signal);

int medusa_signal_set_userdata_ptr (struct medusa_signal *signal, void *userdata);
void * medusa_signal_get_userdata_ptr (struct medusa_signal *signal);

int medusa_signal_set_userdata_int (struct medusa_signal *signal, int userdara);
int medusa_signal_get_userdata_int (struct medusa_signal *signal);

int medusa_signal_set_userdata_uint (struct medusa_signal *signal, unsigned int userdata);
unsigned int medusa_signal_get_userdata_uint (struct medusa_signal *signal);

struct medusa_monitor * medusa_signal_get_monitor (const struct medusa_signal *signal);

const char * medusa_signal_event_string (unsigned int event);

#ifdef __cplusplus
}
#endif

#endif
