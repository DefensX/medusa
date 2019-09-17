
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/tcpsocket.h"
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

static int tcpsocket_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        unsigned int *tevents = (unsigned int *) context;
        (void) tcpsocket;
        (void) param;
        *tevents |= events;
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        int port;
        unsigned int tevents;
        struct medusa_tcpsocket *tcpsocket;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        tevents = 0;
        tcpsocket = medusa_tcpsocket_create(monitor, tcpsocket_onevent, &tevents);
        if (MEDUSA_IS_ERR_OR_NULL(tcpsocket)) {
                fprintf(stderr, "medusa_tcpsocket_create failed\n");
                goto bail;
        }
        rc = medusa_tcpsocket_set_nonblocking(tcpsocket, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_set_nonblocking failed\n");
                goto bail;
        }
        rc = medusa_tcpsocket_set_reuseaddr(tcpsocket, 0);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_set_reuseaddr failed\n");
                goto bail;
        }
        rc = medusa_tcpsocket_set_reuseport(tcpsocket, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_set_reuseport failed\n");
                goto bail;
        }
        for (port = 12345; port < 65535; port++) {
                rc = medusa_tcpsocket_bind(tcpsocket, MEDUSA_TCPSOCKET_PROTOCOL_ANY, "127.0.0.1", port);
                if (rc == 0) {
                        break;
                }
        }
        if (port >= 65535) {
                fprintf(stderr, "medusa_tcpsocket_bind failed\n");
                goto bail;
        }
        fprintf(stderr, "port: %d\n", port);
        rc = medusa_tcpsocket_set_enabled(tcpsocket, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_set_enabled failed\n");
                goto bail;
        }

        medusa_monitor_destroy(monitor);
        monitor = NULL;

        if (tevents != (MEDUSA_TCPSOCKET_EVENT_BINDING |
                        MEDUSA_TCPSOCKET_EVENT_BOUND |
                        MEDUSA_TCPSOCKET_EVENT_LISTENING |
                        MEDUSA_TCPSOCKET_EVENT_DESTROY)) {
                fprintf(stderr, "tevents: 0x%08x is invalid\n", tevents);
                goto bail;
        }
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
