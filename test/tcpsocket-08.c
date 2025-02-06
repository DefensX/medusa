
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#if defined(MEDUSA_TEST_TCPSOCKET_SSL) && (MEDUSA_TEST_TCPSOCKET_SSL == 1)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "medusa/error.h"
#include "medusa/buffer.h"
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

static int tcpsocket_client_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        char c;
        unsigned int *cevents;
        (void) param;
        cevents = (unsigned int *) context;
        fprintf(stderr, "client   events: 0x%08x, %s / 0x%08x\n", events, medusa_tcpsocket_event_string(events), *cevents);
        if (events & MEDUSA_TCPSOCKET_EVENT_RESOLVING) {
                if (*cevents & MEDUSA_TCPSOCKET_EVENT_RESOLVING) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *cevents |= MEDUSA_TCPSOCKET_EVENT_RESOLVING;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_RESOLVING) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_RESOLVED) {
                if (*cevents & MEDUSA_TCPSOCKET_EVENT_RESOLVED) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *cevents |= MEDUSA_TCPSOCKET_EVENT_RESOLVED;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_RESOLVED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTING) {
                if (*cevents & MEDUSA_TCPSOCKET_EVENT_CONNECTING) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *cevents |= MEDUSA_TCPSOCKET_EVENT_CONNECTING;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTING) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                if (*cevents & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *cevents |= MEDUSA_TCPSOCKET_EVENT_CONNECTED;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                if (*cevents & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *cevents |= MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE) {
                if (*cevents & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *cevents |= MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED) {
                if (*cevents & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *cevents |= MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_DISCONNECTED) {
                if (*cevents & MEDUSA_TCPSOCKET_EVENT_DISCONNECTED) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *cevents |= MEDUSA_TCPSOCKET_EVENT_DISCONNECTED;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_DISCONNECTED) {
                        fprintf(stderr, "  invalid state %d != %d\n", medusa_tcpsocket_get_state(tcpsocket), MEDUSA_TCPSOCKET_STATE_DISCONNECTED);
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                fprintf(stderr, "write\n");
                rc = medusa_buffer_append(medusa_tcpsocket_get_write_buffer(tcpsocket), "e", 1);
                if (rc != 1) {
                        fprintf(stderr, "medusa_tcpsocket_write failed\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                rc = medusa_buffer_read_data(medusa_tcpsocket_get_read_buffer(tcpsocket), 0, &c, 1);
                if (rc != 0) {
                        fprintf(stderr, "medusa_tcpsocket_read failed, rc: %d\n", rc);
                        return -1;
                }
                if (c != 'e') {
                        return -1;
                }
                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_DISCONNECTED) {
                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
        }
        return 0;
}

static int tcpsocket_server_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        char c;
        unsigned int *levents;
        (void) param;
        levents = (unsigned int *) context;
        fprintf(stderr, "server   events: 0x%08x, %s / 0x%08x\n", events, medusa_tcpsocket_event_string(events), *levents);
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents |= MEDUSA_TCPSOCKET_EVENT_CONNECTED;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents |= MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents |= MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents |= MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_CONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                fprintf(stderr, "read\n");
                medusa_tcpsocket_destroy(tcpsocket);
                rc = medusa_buffer_read_data(medusa_tcpsocket_get_read_buffer(tcpsocket), 0, &c, 1);
                if (rc != 0) {
                        fprintf(stderr, "medusa_tcpsocket_read failed, rc: %d\n", rc);
                        return -1;
                }
                rc = medusa_buffer_append(medusa_tcpsocket_get_write_buffer(tcpsocket), &c, 1);
                if (rc != 1) {
                        fprintf(stderr, "medusa_tcpsocket_write failed\n");
                        return -1;
                }
        }
        return 0;
}

static int tcpsocket_listener_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        unsigned int *levents;
        struct medusa_tcpsocket *accepted;
        (void) param;
        levents = (unsigned int *) context;
        fprintf(stderr, "listener events: 0x%08x\n", events);
        if (events & MEDUSA_TCPSOCKET_EVENT_DISCONNECTED) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_DISCONNECTED) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents = 0;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_DISCONNECTED) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BINDING) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_BINDING) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents |= MEDUSA_TCPSOCKET_EVENT_BINDING;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_BINDING) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BOUND) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_BOUND) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents |= MEDUSA_TCPSOCKET_EVENT_BOUND;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_BOUND) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_LISTENING) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_LISTENING) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents |= MEDUSA_TCPSOCKET_EVENT_LISTENING;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_LISTENING) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTION) {
                if (*levents & MEDUSA_TCPSOCKET_EVENT_CONNECTION) {
                        fprintf(stderr, "  invalid events\n");
                        return -1;
                }
                *levents |= MEDUSA_TCPSOCKET_EVENT_CONNECTION;
                if (medusa_tcpsocket_get_state(tcpsocket) != MEDUSA_TCPSOCKET_STATE_LISTENING) {
                        fprintf(stderr, "  invalid state\n");
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTION) {
                accepted = medusa_tcpsocket_accept(tcpsocket, tcpsocket_server_onevent, context);
                if (MEDUSA_IS_ERR_OR_NULL(accepted)) {
                        return MEDUSA_PTR_ERR(accepted);
                }
                rc = medusa_tcpsocket_set_buffered(accepted, 1);
                if (rc < 0) {
                        medusa_tcpsocket_destroy(accepted);
                        return -1;
                }
                rc = medusa_tcpsocket_set_nonblocking(accepted, 1);
                if (rc < 0) {
                        medusa_tcpsocket_destroy(accepted);
                        return -1;
                }
                rc = medusa_tcpsocket_set_enabled(accepted, 1);
                if (rc < 0) {
                        medusa_tcpsocket_destroy(accepted);
                        return -1;
                }
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options monitor_init_options;

        unsigned short port;
        unsigned int levents;
        unsigned int cevents;
        struct medusa_tcpsocket *tcpsocket;
        struct medusa_tcpsocket_bind_options tcpsocket_bind_options;
        struct medusa_tcpsocket_connect_options tcpsocket_connect_options;

        monitor = NULL;

#if defined(MEDUSA_TEST_TCPSOCKET_SSL) && (MEDUSA_TEST_TCPSOCKET_SSL == 1)
        SSL_library_init();
        SSL_load_error_strings();
#endif

        medusa_monitor_init_options_default(&monitor_init_options);
        monitor_init_options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&monitor_init_options);
        if (monitor == NULL) {
                goto bail;
        }

        for (port = 12345; port < 65535; port++) {
                fprintf(stderr, "trying port: %d\n", port);

                levents = 0;

                rc = medusa_tcpsocket_bind_options_default(&tcpsocket_bind_options);
                if (rc < 0) {
                        fprintf(stderr, "medusa_tcpsocket_bind_options_default failed\n");
                        goto bail;
                }
                tcpsocket_bind_options.monitor     = monitor;
                tcpsocket_bind_options.onevent     = tcpsocket_listener_onevent;
                tcpsocket_bind_options.context     = &levents;
                tcpsocket_bind_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
                tcpsocket_bind_options.address     = "127.0.0.1";
                tcpsocket_bind_options.port        = port;
                tcpsocket_bind_options.reuseaddr   = 1;
                tcpsocket_bind_options.reuseport   = 0;
                tcpsocket_bind_options.backlog     = 10;
                tcpsocket_bind_options.nonblocking = 1;
                tcpsocket_bind_options.nodelay     = 0;
                tcpsocket_bind_options.buffered    = 1;
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

#if defined(MEDUSA_TEST_TCPSOCKET_SSL) && (MEDUSA_TEST_TCPSOCKET_SSL == 1)
        rc = medusa_tcpsocket_set_ssl_certificate_file(tcpsocket, "tcpsocket-ssl.crt");
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_set_ssl_certificate failed\n");
                goto bail;
        }
        rc = medusa_tcpsocket_set_ssl_privatekey_file(tcpsocket, "tcpsocket-ssl.key");
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_set_ssl_privatekey failed\n");
                goto bail;
        }
        rc = medusa_tcpsocket_set_ssl(tcpsocket, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_set_ssl failed\n");
                goto bail;
        }
#endif

        cevents = 0;

        rc = medusa_tcpsocket_connect_options_default(&tcpsocket_connect_options);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_connect_options_default failed\n");
                goto bail;
        }
        tcpsocket_connect_options.monitor     = monitor;
        tcpsocket_connect_options.onevent     = tcpsocket_client_onevent;
        tcpsocket_connect_options.context     = &cevents;
        tcpsocket_connect_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
        tcpsocket_connect_options.address     = "127.0.0.1";
        tcpsocket_connect_options.port        = port;
        tcpsocket_connect_options.nonblocking = 1;
        tcpsocket_connect_options.nodelay     = 0;
        tcpsocket_connect_options.buffered    = 1;
        tcpsocket_connect_options.enabled     = 1;

        tcpsocket = medusa_tcpsocket_connect_with_options(&tcpsocket_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(tcpsocket)) {
                fprintf(stderr, "medusa_tcpsocket_connect_with_options failed\n");
                goto bail;
        }
        if (medusa_tcpsocket_get_state(tcpsocket) == MEDUSA_TCPSOCKET_STATE_ERROR) {
                fprintf(stderr, "medusa_tcpsocket_connect_with_options error: %d, %s\n", medusa_tcpsocket_get_error(tcpsocket), strerror(medusa_tcpsocket_get_error(tcpsocket)));
                goto bail;
        }

#if defined(MEDUSA_TEST_TCPSOCKET_SSL) && (MEDUSA_TEST_TCPSOCKET_SSL == 1)
        rc = medusa_tcpsocket_set_ssl(tcpsocket, 1);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_set_ssl failed\n");
                goto bail;
        }
#endif

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "medusa_monitor_run failed\n");
                goto bail;
        }

        if (levents != (MEDUSA_TCPSOCKET_EVENT_BINDING |
                        MEDUSA_TCPSOCKET_EVENT_BOUND |
                        MEDUSA_TCPSOCKET_EVENT_LISTENING |
                        MEDUSA_TCPSOCKET_EVENT_CONNECTION |
                        MEDUSA_TCPSOCKET_EVENT_CONNECTED |
                        MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ)) {
                fprintf(stderr, "listener events: 0x%08x is invalid\n", levents);
                goto bail;
        }
        if (cevents != (MEDUSA_TCPSOCKET_EVENT_RESOLVING |
                        MEDUSA_TCPSOCKET_EVENT_RESOLVED |
                        MEDUSA_TCPSOCKET_EVENT_CONNECTING |
                        MEDUSA_TCPSOCKET_EVENT_CONNECTED |
                        MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE |
                        MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED |
                        MEDUSA_TCPSOCKET_EVENT_DISCONNECTED)) {
                fprintf(stderr, "client   events: 0x%08x invalid\n", cevents);
                goto bail;
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

                fprintf(stderr, "testing poll: %d\n", g_polls[i]);
                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        fprintf(stderr, "failed\n");
                        return -1;
                }
                fprintf(stderr, "success\n");
        }
        return 0;
}
