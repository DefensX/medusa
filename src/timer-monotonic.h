
#if !defined(MEDUSA_TIMER_MONOTONIC_H)
#define MEDUSA_TIMER_MONOTONIC_H

struct medusa_timer_monotonic_init_options {

};

struct medusa_timer_backend * medusa_timer_monotonic_create (const struct medusa_timer_monotonic_init_options *options);

#endif
