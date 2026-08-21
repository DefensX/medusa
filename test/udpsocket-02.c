
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "medusa/error.h"
#include "medusa/udpsocket.h"
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

static int udpsocket_bind_onevent (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param)
{
        (void) udpsocket;
        (void) events;
        (void) context;
        (void) param;
        fprintf(stderr, "bind    events: 0x%08x, %s\n", events, medusa_udpsocket_event_string(events));
        if (events & MEDUSA_UDPSOCKET_EVENT_IN) {
                return medusa_monitor_break(medusa_udpsocket_get_monitor(udpsocket));
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_ERROR) {
                fprintf(stderr, "          error: %d, %s\n", medusa_udpsocket_get_error(udpsocket), strerror(medusa_udpsocket_get_error(udpsocket)));
        }
        return 0;
}

static int udpsocket_connect_onevent (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        (void) udpsocket;
        (void) events;
        (void) context;
        (void) param;
        fprintf(stderr, "connect events: 0x%08x, %s\n", events, medusa_udpsocket_event_string(events));
        if (events & MEDUSA_UDPSOCKET_EVENT_ERROR) {
                fprintf(stderr, "          error: %d, %s\n", medusa_udpsocket_get_error(udpsocket), strerror(medusa_udpsocket_get_error(udpsocket)));
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_CONNECTED) {
                rc = sendto(medusa_udpsocket_get_fd(udpsocket), "e", 1, 0, NULL, 0);
                if (rc != 1) {
                        return -1;
                }
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        int port;
        struct medusa_udpsocket *udpsocket;
        struct medusa_udpsocket_bind_options udpsocket_bind_options;
        struct medusa_udpsocket_connect_options udpsocket_connect_options;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                goto bail;
        }

        for (port = 12345; port < 65535; port++) {
                fprintf(stderr, "trying port: %d\n", port);

                rc = medusa_udpsocket_bind_options_default(&udpsocket_bind_options);
                if (rc < 0) {
                        fprintf(stderr, "medusa_udpsocket_bind_options_default failed\n");
                        goto bail;
                }
                udpsocket_bind_options.monitor     = monitor;
                udpsocket_bind_options.onevent     = udpsocket_bind_onevent;
                udpsocket_bind_options.context     = NULL;
                udpsocket_bind_options.protocol    = MEDUSA_UDPSOCKET_PROTOCOL_ANY;
                udpsocket_bind_options.address     = "127.0.0.1";
                udpsocket_bind_options.port        = port;
                udpsocket_bind_options.reuseaddr   = 1;
                udpsocket_bind_options.reuseport   = 0;
                udpsocket_bind_options.nonblocking = 1;
                udpsocket_bind_options.enabled     = 1;

                udpsocket = medusa_udpsocket_bind_with_options(&udpsocket_bind_options);
                if (MEDUSA_IS_ERR_OR_NULL(udpsocket)) {
                        fprintf(stderr, "medusa_udpsocket_bind_with_options failed\n");
                        goto bail;
                }
                if (medusa_udpsocket_get_state(udpsocket) == MEDUSA_UDPSOCKET_STATE_DISCONNECTED) {
                        fprintf(stderr, "medusa_udpsocket_bind_with_options error: %d, %s\n", medusa_udpsocket_get_error(udpsocket), strerror(medusa_udpsocket_get_error(udpsocket)));
                        medusa_udpsocket_destroy(udpsocket);
                } else {
                        break;
                }
        }
        if (port >= 65535) {
                fprintf(stderr, "medusa_udpsocket_bind failed\n");
                goto bail;
        }
        fprintf(stderr, "port: %d\n", port);

        rc = medusa_udpsocket_connect_options_default(&udpsocket_connect_options);
        if (rc < 0) {
                fprintf(stderr, "medusa_udpsocket_connect_options_default failed\n");
                goto bail;
        }
        udpsocket_connect_options.monitor     = monitor;
        udpsocket_connect_options.onevent     = udpsocket_connect_onevent;
        udpsocket_connect_options.context     = NULL;
        udpsocket_connect_options.protocol    = MEDUSA_UDPSOCKET_PROTOCOL_ANY;
        udpsocket_connect_options.address     = "127.0.0.1";
        udpsocket_connect_options.port        = port;
        udpsocket_connect_options.nonblocking = 1;
        udpsocket_connect_options.enabled     = 1;

        udpsocket = medusa_udpsocket_connect_with_options(&udpsocket_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(udpsocket)) {
                fprintf(stderr, "medusa_udpsocket_connect_with_options failed\n");
                goto bail;
        }
        if (medusa_udpsocket_get_state(udpsocket) == MEDUSA_UDPSOCKET_STATE_DISCONNECTED) {
                fprintf(stderr, "medusa_udpsocket_connect_with_options error: %d, %s\n", medusa_udpsocket_get_error(udpsocket), strerror(medusa_udpsocket_get_error(udpsocket)));
                goto bail;
        }

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "medusa_monitor_run failed: %d\n", rc);
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
