
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#if defined(__WINDOWS__)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include "medusa/error.h"
#include "medusa/tcpsocket.h"
#include "medusa/monitor.h"

/*
 * MEDUSA_TCPSOCKET_EVENT_CONNECT_SOCKET: given after connecting, before
 * connected, with the socket that then connects, not connected yet. a
 * failing handler fails the connect: error event, the socket is closed,
 * nothing reaches the listener.
 */

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
        MEDUSA_MONITOR_POLL_IOCP,
#endif
};

struct test {
        int fail;
        int fd;
        unsigned int cevents;
        unsigned int levents;
};

static int tcpsocket_client_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        int type;
        socklen_t length;
        struct sockaddr_storage peer;
        struct test *test = (struct test *) context;
        fprintf(stderr, "client   events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTING) {
                test->cevents |= MEDUSA_TCPSOCKET_EVENT_CONNECTING;
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECT_SOCKET) {
                struct medusa_tcpsocket_event_connect_socket *medusa_tcpsocket_event_connect_socket = (struct medusa_tcpsocket_event_connect_socket *) param;
                if ((test->cevents & MEDUSA_TCPSOCKET_EVENT_CONNECTING) == 0 ||
                    (test->cevents & (MEDUSA_TCPSOCKET_EVENT_CONNECT_SOCKET | MEDUSA_TCPSOCKET_EVENT_CONNECTED)) != 0) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                test->cevents |= MEDUSA_TCPSOCKET_EVENT_CONNECT_SOCKET;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTING) {
                        fprintf(stderr, "  invalid state: %d, %s\n", medusa_tcpsocket_get_state(tcpsocket), medusa_tcpsocket_state_string(medusa_tcpsocket_get_state(tcpsocket)));
                        return -1;
                }
                if (medusa_tcpsocket_event_connect_socket == NULL ||
                    medusa_tcpsocket_event_connect_socket->fd < 0) {
                        fprintf(stderr, "  invalid fd\n");
                        return -1;
                }
                test->fd = medusa_tcpsocket_event_connect_socket->fd;
                length = sizeof(type);
                rc = getsockopt(test->fd, SOL_SOCKET, SO_TYPE, (char *) &type, &length);
                if (rc != 0 || type != SOCK_STREAM) {
                        fprintf(stderr, "  fd is not a stream socket\n");
                        return -1;
                }
                length = sizeof(peer);
                rc = getpeername(test->fd, (struct sockaddr *) &peer, &length);
                if (rc == 0) {
                        fprintf(stderr, "  fd is connected already\n");
                        return -1;
                }
                if (test->fail) {
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                test->cevents |= MEDUSA_TCPSOCKET_EVENT_CONNECTED;
                if (test->fail) {
                        fprintf(stderr, "  connected, the connect was to fail\n");
                        return -1;
                }
                if (medusa_tcpsocket_get_fd(tcpsocket) != test->fd) {
                        fprintf(stderr, "  connected with fd: %d, not %d\n", medusa_tcpsocket_get_fd(tcpsocket), test->fd);
                        return -1;
                }
                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_ERROR) {
                test->cevents |= MEDUSA_TCPSOCKET_EVENT_ERROR;
                if (!test->fail) {
                        fprintf(stderr, "  error\n");
                        return -1;
                }
                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
        }
        return 0;
}

static int tcpsocket_server_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        (void) tcpsocket;
        (void) events;
        (void) context;
        (void) param;
        return 0;
}

static int tcpsocket_listener_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        struct medusa_tcpsocket *accepted;
        struct test *test = (struct test *) context;
        (void) param;
        fprintf(stderr, "listener events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTION) {
                test->levents |= MEDUSA_TCPSOCKET_EVENT_CONNECTION;
                accepted = medusa_tcpsocket_accept(tcpsocket, tcpsocket_server_onevent, NULL);
                if (MEDUSA_IS_ERR_OR_NULL(accepted)) {
                        return MEDUSA_PTR_ERR(accepted);
                }
                medusa_tcpsocket_destroy(accepted);
        }
        return 0;
}

static int test_connect (unsigned int poll, int fail)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options monitor_init_options;

        unsigned short port;
        struct test test;
        struct medusa_tcpsocket *tcpsocket;
        struct medusa_tcpsocket_bind_options tcpsocket_bind_options;
        struct medusa_tcpsocket_connect_options tcpsocket_connect_options;

        monitor = NULL;

        memset(&test, 0, sizeof(test));
        test.fail = fail;
        test.fd   = -1;

        medusa_monitor_init_options_default(&monitor_init_options);
        monitor_init_options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&monitor_init_options);
        if (monitor == NULL) {
                goto bail;
        }

        for (port = 12345; port < 65535; port++) {
                fprintf(stderr, "trying port: %d\n", port);

                rc = medusa_tcpsocket_bind_options_default(&tcpsocket_bind_options);
                if (rc < 0) {
                        fprintf(stderr, "medusa_tcpsocket_bind_options_default failed\n");
                        goto bail;
                }
                tcpsocket_bind_options.monitor     = monitor;
                tcpsocket_bind_options.onevent     = tcpsocket_listener_onevent;
                tcpsocket_bind_options.context     = &test;
                tcpsocket_bind_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
                tcpsocket_bind_options.address     = "127.0.0.1";
                tcpsocket_bind_options.port        = port;
                tcpsocket_bind_options.reuseaddr   = 1;
                tcpsocket_bind_options.reuseport   = 0;
                tcpsocket_bind_options.backlog     = 10;
                tcpsocket_bind_options.nonblocking = 1;
                tcpsocket_bind_options.enabled     = 1;

                tcpsocket = medusa_tcpsocket_bind_with_options(&tcpsocket_bind_options);
                if (MEDUSA_IS_ERR_OR_NULL(tcpsocket)) {
                        fprintf(stderr, "medusa_tcpsocket_bind_with_options failed\n");
                        goto bail;
                }
                if (medusa_tcpsocket_get_state(tcpsocket) == MEDUSA_TCPSOCKET_STATE_ERROR) {
                        fprintf(stderr, "medusa_tcpsocket_bind_with_options error: %d, %s\n", medusa_tcpsocket_get_error(tcpsocket), strerror(medusa_tcpsocket_get_error(tcpsocket)));
                        medusa_tcpsocket_destroy(tcpsocket);
                } else {
                        break;
                }
        }
        if (port >= 65535) {
                fprintf(stderr, "medusa_tcpsocket_bind failed\n");
                goto bail;
        }
        fprintf(stderr, "port: %d\n", port);

        rc = medusa_tcpsocket_connect_options_default(&tcpsocket_connect_options);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_connect_options_default failed\n");
                goto bail;
        }
        tcpsocket_connect_options.monitor     = monitor;
        tcpsocket_connect_options.onevent     = tcpsocket_client_onevent;
        tcpsocket_connect_options.context     = &test;
        tcpsocket_connect_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
        tcpsocket_connect_options.address     = "127.0.0.1";
        tcpsocket_connect_options.port        = port;
        tcpsocket_connect_options.nonblocking = 1;
        tcpsocket_connect_options.enabled     = 1;

        tcpsocket = medusa_tcpsocket_connect_with_options(&tcpsocket_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(tcpsocket)) {
                fprintf(stderr, "medusa_tcpsocket_connect_with_options failed\n");
                goto bail;
        }

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "medusa_monitor_run failed\n");
                goto bail;
        }

        if (fail) {
                if (test.cevents != (MEDUSA_TCPSOCKET_EVENT_CONNECTING |
                                     MEDUSA_TCPSOCKET_EVENT_CONNECT_SOCKET |
                                     MEDUSA_TCPSOCKET_EVENT_ERROR)) {
                        fprintf(stderr, "client   events: 0x%08x invalid\n", test.cevents);
                        goto bail;
                }
                if (test.levents != 0) {
                        fprintf(stderr, "listener events: 0x%08x, the failed connect reached it\n", test.levents);
                        goto bail;
                }
        } else {
                if (test.cevents != (MEDUSA_TCPSOCKET_EVENT_CONNECTING |
                                     MEDUSA_TCPSOCKET_EVENT_CONNECT_SOCKET |
                                     MEDUSA_TCPSOCKET_EVENT_CONNECTED)) {
                        fprintf(stderr, "client   events: 0x%08x invalid\n", test.cevents);
                        goto bail;
                }
        }

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

static void sigalarm_handler (int sig)
{
        (void) sig;
        abort();
}

static void sigint_handler (int sig)
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
        signal(SIGALRM, sigalarm_handler);
        signal(SIGINT, sigint_handler);

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                alarm(5);

                fprintf(stderr, "testing poll: %d, connect\n", g_polls[i]);
                rc = test_connect(g_polls[i], 0);
                if (rc != 0) {
                        fprintf(stderr, "failed\n");
                        return -1;
                }
                fprintf(stderr, "testing poll: %d, failing connect socket\n", g_polls[i]);
                rc = test_connect(g_polls[i], 1);
                if (rc != 0) {
                        fprintf(stderr, "failed\n");
                        return -1;
                }
                fprintf(stderr, "success\n");
        }
        return 0;
}
