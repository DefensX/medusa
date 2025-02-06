
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/httprequest.h"
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
        MEDUSA_MONITOR_POLL_SELECT
};

static int httprequest_onevent (struct medusa_httprequest *httprequest, unsigned int events, void *context, void *param)
{
        (void) httprequest;
        (void) events;
        (void) context;
        (void) param;
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        struct medusa_httprequest *httprequest;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        httprequest = medusa_httprequest_create(monitor, httprequest_onevent, NULL);
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                fprintf(stderr, "medusa_httprequest_create failed\n");
                goto bail;
        }

        rc = medusa_httprequest_add_header(httprequest, "key", "value");
        if (rc < 0) {
                fprintf(stderr, "medusa_httprequest_add_header failed\n");
                goto bail;
        }
        rc = medusa_httprequest_add_headerf(httprequest, "key", "%s", "value");
        if (rc < 0) {
                fprintf(stderr, "medusa_httprequest_add_header failed\n");
                goto bail;
        }
        rc = medusa_httprequest_add_headerf(httprequest, "key", "%s.%d", "value", 0);
        if (rc < 0) {
                fprintf(stderr, "medusa_httprequest_add_header failed\n");
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
