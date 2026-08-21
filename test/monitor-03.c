
#include <unistd.h>
#include <string.h>

#include "medusa/monitor.h"

static const unsigned int polls[] = {
        MEDUSA_MONITOR_POLL_DEFAULT,
#if defined(__LINUX__)
        MEDUSA_MONITOR_POLL_EPOLL,
#endif
#if defined(__APPLE__)
        MEDUSA_MONITOR_POLL_KQUEUE,
#endif
        MEDUSA_MONITOR_POLL_POLL,
        MEDUSA_MONITOR_POLL_SELECT,
#if defined(__WINDOWS__)
        MEDUSA_MONITOR_POLL_WSAPOLL,
#endif
};

int main (int argc, char *argv[])
{
        unsigned int i;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;
        (void) argc;
        (void) argv;
        for (i = 0; i < sizeof(polls) / sizeof(polls[0]); i++) {
                medusa_monitor_init_options_default(&options);
                options.poll.type = polls[i];
                monitor = medusa_monitor_create_with_options(&options);
                if (monitor == NULL) {
                        return -1;
                }
                medusa_monitor_destroy(monitor);
        }
        return 0;
}
