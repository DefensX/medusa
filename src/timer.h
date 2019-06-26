
#if !defined(MEDUSA_TIMER_H)
#define MEDUSA_TIMER_H

enum {
        MEDUSA_TIMER_RESOLUTION_NANOSECOMDS     = 1,
        MEDUSA_TIMER_RESOLUTION_MICROSECONDS    = 2,
        MEDUSA_TIMER_RESOLUTION_MILLISECONDS    = 3,
        MEDUSA_TIMER_RESOLUTION_SECONDS         = 4,
        MEDUSA_TIMER_RESOLUTION_DEFAULT         = MEDUSA_TIMER_RESOLUTION_MILLISECONDS
#define MEDUSA_TIMER_RESOLUTION_DEFAULT         MEDUSA_TIMER_RESOLUTION_DEFAULT
#define MEDUSA_TIMER_RESOLUTION_NANOSECOMDS     MEDUSA_TIMER_RESOLUTION_NANOSECOMDS
#define MEDUSA_TIMER_RESOLUTION_MICROSECONDS    MEDUSA_TIMER_RESOLUTION_MICROSECONDS
#define MEDUSA_TIMER_RESOLUTION_MILLISECONDS    MEDUSA_TIMER_RESOLUTION_MILLISECONDS
#define MEDUSA_TIMER_RESOLUTION_SECONDS         MEDUSA_TIMER_RESOLUTION_SECONDS
};

enum {
        MEDUSA_TIMER_EVENT_TIMEOUT      = (1 << 0), /* 0x00000001 */
        MEDUSA_TIMER_EVENT_DESTROY      = (1 << 1), /* 0x00000002 */
#define MEDUSA_TIMER_EVENT_TIMEOUT      MEDUSA_TIMER_EVENT_TIMEOUT
#define MEDUSA_TIMER_EVENT_DESTROY      MEDUSA_TIMER_EVENT_DESTROY
};

struct timeval;
struct timespec;
struct medusa_timer;
struct medusa_monitor;

struct medusa_timer_init_options {
        struct medusa_monitor *monitor;
        int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, ...);
        void *context;
        double initial;
        double interval;
        int singleshot;
        unsigned int resolution;
        int enabled;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_timer_init_options_default (struct medusa_timer_init_options *options);

int medusa_timer_create_singleshot (struct medusa_monitor *monitor, double interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, ...), void *context);
int medusa_timer_create_singleshot_timeval (struct medusa_monitor *monitor, const struct timeval *interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, ...), void *context);
int medusa_timer_create_singleshot_timespec (struct medusa_monitor *monitor, const struct timespec *interval, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, ...), void *context);

struct medusa_timer * medusa_timer_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_timer *timer, unsigned int events, void *context, ...), void *context);
struct medusa_timer * medusa_timer_create_with_options (const struct medusa_timer_init_options *options);
void medusa_timer_destroy (struct medusa_timer *timer);

int medusa_timer_set_initial (struct medusa_timer *timer, double initial);
int medusa_timer_set_initial_timeval (struct medusa_timer *timer, const struct timeval *initial);
int medusa_timer_set_initial_timespec (struct medusa_timer *timer, const struct timespec *initial);
double medusa_timer_get_initial (const struct medusa_timer *timer);

int medusa_timer_set_interval (struct medusa_timer *timer, double interval);
int medusa_timer_set_interval_timeval (struct medusa_timer *timer, const struct timeval *interval);
int medusa_timer_set_interval_timespec (struct medusa_timer *timer, const struct timespec *interval);
double medusa_timer_get_interval (const struct medusa_timer *timer);

double medusa_timer_get_remaining_time (const struct medusa_timer *timer);
int medusa_timer_get_remaining_timeval (const struct medusa_timer *timer, struct timeval *timeval);
int medusa_timer_get_remaining_timespec (const struct medusa_timer *timer, struct timespec *timespec);

int medusa_timer_set_singleshot (struct medusa_timer *timer, int singleshot);
int medusa_timer_get_singleshot (const struct medusa_timer *timer);

int medusa_timer_set_resolution (struct medusa_timer *timer, unsigned int resolution);
unsigned int medusa_timer_get_resolution (const struct medusa_timer *timer);

int medusa_timer_set_enabled (struct medusa_timer *timer, int enabled);
int medusa_timer_get_enabled (const struct medusa_timer *timer);

int medusa_timer_enable (struct medusa_timer *timer);
int medusa_timer_disable (struct medusa_timer *timer);

int medusa_timer_start (struct medusa_timer *timer);
int medusa_timer_stop (struct medusa_timer *timer);

int medusa_timer_set_userdata (struct medusa_timer *timer, void *userdata);
void * medusa_timer_get_userdata (struct medusa_timer *timer);

struct medusa_monitor * medusa_timer_get_monitor (const struct medusa_timer *timer);

const char * medusa_timer_event_string (unsigned int events);

#ifdef __cplusplus
}
#endif

#endif
