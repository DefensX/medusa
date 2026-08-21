
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/dnsrequest.h"
#include "medusa/monitor.h"

static const unsigned int g_polls[] = {
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

static int dnsrequest_onevent (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param)
{
        (void) dnsrequest;
        (void) events;
        (void) context;
        (void) param;
        fprintf(stderr, "events: 0x%08x %s\n", events, medusa_dnsrequest_event_string(events));
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct medusa_dnsrequest *dnsrequest;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        dnsrequest = medusa_dnsrequest_create(monitor, dnsrequest_onevent, NULL);
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                fprintf(stderr, "medusa_dnsrequest_create failed\n");
                goto bail;
        }

        rc = medusa_dnsrequest_set_nameserver(dnsrequest, "8.8.8.8");
        if (rc < 0) {
                fprintf(stderr, "medusa_dnsrequest_set_nameserver failed\n");
                goto bail;
        }
        rc = medusa_dnsrequest_set_type(dnsrequest, MEDUSA_DNSREQUEST_RECORD_TYPE_A);
        if (rc < 0) {
                fprintf(stderr, "medusa_dnsrequest_set_type failed\n");
                goto bail;
        }
        rc = medusa_dnsrequest_set_name(dnsrequest, "www.google.com");
        if (rc < 0) {
                fprintf(stderr, "medusa_dnsrequest_set_name failed\n");
                goto bail;
        }

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

static void alarm_handler (int sig)
{
        (void) sig;
        abort();
}

int main (int argc, char *argv[])
{
        int rc;
        unsigned int i;

        (void) argc;
        (void) argv;

        srand(time(NULL));
        signal(SIGALRM, alarm_handler);

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                alarm(5);

                fprintf(stderr, "testing poll: %d\n", g_polls[i]);
                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        fprintf(stderr, "  failed\n");
                        return -1;
                }
                fprintf(stderr, "success\n");
        }
        return 0;
}
