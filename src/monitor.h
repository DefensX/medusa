
#if !defined(MEDUSA_MONITOR_H)
#define MEDUSA_MONITOR_H

#ifdef __cplusplus
extern "C"
{
#endif

struct medusa_monitor;

enum {
        MEDUSA_MONITOR_POLL_DEFAULT,
        MEDUSA_MONITOR_POLL_EPOLL,
        MEDUSA_MONITOR_POLL_KQUEUE,
        MEDUSA_MONITOR_POLL_POLL,
        MEDUSA_MONITOR_POLL_SELECT
#define MEDUSA_MONITOR_POLL_DEFAULT     MEDUSA_MONITOR_POLL_DEFAULT
#define MEDUSA_MONITOR_POLL_EPOLL       MEDUSA_MONITOR_POLL_EPOLL
#define MEDUSA_MONITOR_POLL_KQUEUE      MEDUSA_MONITOR_POLL_KQUEUE
#define MEDUSA_MONITOR_POLL_POLL        MEDUSA_MONITOR_POLL_POLL
#define MEDUSA_MONITOR_POLL_SELECT      MEDUSA_MONITOR_POLL_SELECT
};

enum {
        MEDUSA_MONITOR_TIMER_DEFAULT,
        MEDUSA_MONITOR_TIMER_TIMERFD,
        MEDUSA_MONITOR_TIMER_MONOTONIC
#define MEDUSA_MONITOR_TIMER_DEFAULT    MEDUSA_MONITOR_TIMER_DEFAULT
#define MEDUSA_MONITOR_TIMER_TIMERFD    MEDUSA_MONITOR_TIMER_TIMERFD
#define MEDUSA_MONITOR_TIMER_MONOTONIC  MEDUSA_MONITOR_TIMER_MONOTONIC
};

enum {
        MEDUSA_MONITOR_SIGNAL_DEFAULT,
        MEDUSA_MONITOR_SIGNAL_SIGACTION,
        MEDUSA_MONITOR_SIGNAL_NULL
#define MEDUSA_MONITOR_SIGNAL_DEFAULT   MEDUSA_MONITOR_SIGNAL_DEFAULT
#define MEDUSA_MONITOR_SIGNAL_SIGACTION MEDUSA_MONITOR_SIGNAL_SIGACTION
#define MEDUSA_MONITOR_SIGNAL_NULL      MEDUSA_MONITOR_SIGNAL_NULL
};

enum {
        MEDUSA_MONITOR_FLAG_NONE        = 0x00000000,
        MEDUSA_MONITOR_FLAG_THREAD_SAFE = 0x00000001,
        MEDUSA_MONITOR_FLAG_DEFAULT     = MEDUSA_MONITOR_FLAG_THREAD_SAFE
#define MEDUSA_MONITOR_FLAG_NONE        MEDUSA_MONITOR_FLAG_NONE
#define MEDUSA_MONITOR_FLAG_THREAD_SAFE MEDUSA_MONITOR_FLAG_THREAD_SAFE
#define MEDUSA_MONITOR_FLAG_DEFAULT     MEDUSA_MONITOR_FLAG_DEFAULT
};

enum {
        MEDUSA_MONITOR_EVENT_ERROR      = (1 <<  0), /* 0x00000001 */
        MEDUSA_MONITOR_EVENT_DESTROY    = (1 <<  1), /* 0x00000002 */
#define MEDUSA_MONITOR_EVENT_ERROR      MEDUSA_MONITOR_EVENT_ERROR
#define MEDUSA_MONITOR_EVENT_DESTROY    MEDUSA_MONITOR_EVENT_DESTROY
};

struct medusa_monitor_init_options {
        unsigned int flags;
        struct {
                unsigned int type;
                union {
                        struct {
                                int foo;
                        } epoll;
                        struct {
                                int foo;
                        } kqueue;
                        struct {
                                int foo;
                        } poll;
                        struct {
                                int foo;
                        } select;
                } u;
        } poll;
        struct {
                unsigned int type;
                union {
                        struct {
                                int foo;
                        } timerfd;
                } u;
        } timer;
        struct {
                unsigned int type;
                union {
                        struct {
                                int foo;
                        } sigaction;
                } u;
        } signal;
        struct {
                int (*callback) (struct medusa_monitor *monitor, unsigned int events, void *context, void *param);
                void *context;
        } onevent;
};

enum {
        MEDUSA_MONITOR_ERROR_TYPE_UNKNOWN               = 0,
        MEDUSA_MONITOR_ERROR_TYPE_SUBJECT_ONEVENT       = 1
#define MEDUSA_MONITOR_ERROR_TYPE_UNKNOWN               MEDUSA_MONITOR_ERROR_TYPE_UNKNOWN
#define MEDUSA_MONITOR_ERROR_TYPE_SUBJECT_ONEVENT       MEDUSA_MONITOR_ERROR_TYPE_SUBJECT_ONEVENT
};

struct medusa_monitor_event_error_subject_onevent {
        unsigned int subject_type;
};

struct medusa_monitor_event_error {
        unsigned int type;
        union {
                struct medusa_monitor_event_error_subject_onevent subject_onevent;
        } u;
};

int medusa_monitor_init_options_default (struct medusa_monitor_init_options *options);

struct medusa_monitor * medusa_monitor_create_with_options (const struct medusa_monitor_init_options *options);
struct medusa_monitor * medusa_monitor_create (void);
void medusa_monitor_destroy (struct medusa_monitor *monitor);

int medusa_monitor_run (struct medusa_monitor *monitor);
int medusa_monitor_run_once (struct medusa_monitor *monitor);
int medusa_monitor_run_timeout (struct medusa_monitor *monitor, double timeout);

int medusa_monitor_get_running (struct medusa_monitor *monitor);

int medusa_monitor_break (struct medusa_monitor *monitor);
int medusa_monitor_continue (struct medusa_monitor *monitor);

const char * medusa_monitor_event_string (unsigned int event);

#ifdef __cplusplus
}
#endif

#endif
