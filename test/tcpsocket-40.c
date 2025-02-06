
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
        (void) tcpsocket;
        (void) context;
        (void) param;
        fprintf(stderr, "client events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                unsigned int *connected = context;
                *connected = *connected | 1;
                fprintf(stderr, "  write\n");
                rc = medusa_buffer_append(medusa_tcpsocket_get_write_buffer(tcpsocket), "e", 1);
                if (rc != 1) {
                        fprintf(stderr, "medusa_tcpsocket_write failed: %d, %s\n", rc, medusa_strerror(rc));
                        return -1;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                fprintf(stderr, "  read\n");
                rc = medusa_buffer_read_data(medusa_tcpsocket_get_read_buffer(tcpsocket), 0, &c, 1);
                if (rc != 0) {
                        fprintf(stderr, "medusa_tcpsocket_read failed: %d, %s\n", rc, medusa_strerror(rc));
                        return -1;
                }
                if (c != 'e') {
                        return -1;
                }
                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
        }
        return 0;
}

static int tcpsocket_server_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        char c;
        (void) tcpsocket;
        (void) context;
        (void) param;
        fprintf(stderr, "server events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                unsigned int *connected = context;
                *connected = *connected | 2;
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                fprintf(stderr, "  read\n");
                rc = medusa_buffer_read_data(medusa_tcpsocket_get_read_buffer(tcpsocket), 0, &c, 1);
                if (rc != 0) {
                        fprintf(stderr, "medusa_tcpsocket_read failed: %d, %s\n", rc, medusa_strerror(rc));
                        return -1;
                }
                if (c != 'e') {
                        fprintf(stderr, "medusa_tcpsocket_read failed: c != 'e'\n");
                        return -1;
                }
                fprintf(stderr, "  write\n");
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
        struct medusa_tcpsocket *accepted;
        (void) tcpsocket;
        (void) events;
        (void) context;
        (void) param;
        fprintf(stderr, "bind   events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
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
#if defined(MEDUSA_TEST_TCPSOCKET_SSL) && (MEDUSA_TEST_TCPSOCKET_SSL == 1)
                rc = medusa_tcpsocket_set_ssl_certificate_file(accepted, "tcpsocket-ssl.crt");
                if (rc < 0) {
                        fprintf(stderr, "medusa_tcpsocket_set_ssl_certificate failed\n");
                        medusa_tcpsocket_destroy(accepted);
                        return -1;
                }
                rc = medusa_tcpsocket_set_ssl_privatekey_file(accepted, "tcpsocket-ssl.key");
                if (rc < 0) {
                        fprintf(stderr, "medusa_tcpsocket_set_ssl_privatekey failed\n");
                        medusa_tcpsocket_destroy(accepted);
                        return -1;
                }
                rc = medusa_tcpsocket_set_ssl(accepted, 1);
                if (rc < 0) {
                        fprintf(stderr, "medusa_tcpsocket_set_ssl failed\n");
                        medusa_tcpsocket_destroy(accepted);
                        return -1;
                }
#endif
        }
        return 0;
}

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options monitor_init_options;

        unsigned short port;
        unsigned int connected;
        struct medusa_tcpsocket *tcpsocket;
        struct medusa_tcpsocket_bind_options tcpsocket_bind_options;
        struct medusa_tcpsocket_connect_options tcpsocket_connect_options;

        monitor = NULL;
        connected = 0;

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

                rc = medusa_tcpsocket_bind_options_default(&tcpsocket_bind_options);
                if (rc < 0) {
                        fprintf(stderr, "medusa_tcpsocket_bind_options_default failed\n");
                        goto bail;
                }
                tcpsocket_bind_options.monitor     = monitor;
                tcpsocket_bind_options.onevent     = tcpsocket_listener_onevent;
                tcpsocket_bind_options.context     = &connected;
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

        rc = medusa_tcpsocket_connect_options_default(&tcpsocket_connect_options);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_connect_options_default failed\n");
                goto bail;
        }
        tcpsocket_connect_options.monitor     = monitor;
        tcpsocket_connect_options.onevent     = tcpsocket_client_onevent;
        tcpsocket_connect_options.context     = &connected;
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
