
#if defined(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) && (MEDUSA_TCPSOCKET_OPENSSL_ENABLE ==1)

/*
 * a callback may change errno: CONNECTED_SSL handlers set it on both ends,
 * the exchange after the handshake must still work.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

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
        MEDUSA_MONITOR_POLL_SELECT,
#if defined(__WINDOWS__)
        MEDUSA_MONITOR_POLL_WSAPOLL,
        MEDUSA_MONITOR_POLL_IOCP,
#endif
};

#define TEST_MESSAGE    "ping"

struct test_context {
        int ssl_connected;
        int echoed;
        int failed;
};

static int tcpsocket_client_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        int64_t length;
        struct test_context *test = context;
        (void) param;
        fprintf(stderr, "client   events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                rc = medusa_tcpsocket_write(tcpsocket, TEST_MESSAGE, strlen(TEST_MESSAGE));
                if (rc != (int) strlen(TEST_MESSAGE)) {
                        test->failed = 1;
                        return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED_SSL) {
                test->ssl_connected |= 1;
                errno = EACCES;
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                length = medusa_buffer_get_length(medusa_tcpsocket_get_read_buffer(tcpsocket));
                if (length >= (int64_t) strlen(TEST_MESSAGE)) {
                        if (memcmp(medusa_buffer_linearize(medusa_tcpsocket_get_read_buffer(tcpsocket), 0, length), TEST_MESSAGE, strlen(TEST_MESSAGE)) != 0) {
                                test->failed = 1;
                        }
                        test->echoed = 1;
                        return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                }
        }
        if (events & (MEDUSA_TCPSOCKET_EVENT_ERROR | MEDUSA_TCPSOCKET_EVENT_DISCONNECTED)) {
                test->failed = 1;
                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
        }
        return 0;
}

static int tcpsocket_server_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        int64_t length;
        struct test_context *test = context;
        (void) param;
        fprintf(stderr, "server   events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED_SSL) {
                test->ssl_connected |= 2;
                errno = EACCES;
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                length = medusa_buffer_get_length(medusa_tcpsocket_get_read_buffer(tcpsocket));
                if (length > 0) {
                        rc = medusa_tcpsocket_write(tcpsocket, medusa_buffer_linearize(medusa_tcpsocket_get_read_buffer(tcpsocket), 0, length), length);
                        if (rc != length) {
                                test->failed = 1;
                                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                        }
                        medusa_buffer_choke(medusa_tcpsocket_get_read_buffer(tcpsocket), 0, length);
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_ERROR) {
                test->failed = 1;
                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
        }
        return 0;
}

static int tcpsocket_listener_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        struct medusa_tcpsocket *accepted;
        struct medusa_tcpsocket_accept_options tcpsocket_accept_options;
        (void) param;
        fprintf(stderr, "listener events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTION) {
                medusa_tcpsocket_accept_options_default(&tcpsocket_accept_options);
                tcpsocket_accept_options.onevent     = tcpsocket_server_onevent;
                tcpsocket_accept_options.context     = context;
                tcpsocket_accept_options.nonblocking = 1;
                tcpsocket_accept_options.buffered    = 1;
                tcpsocket_accept_options.enabled     = 1;
                accepted = medusa_tcpsocket_accept_with_options(tcpsocket, &tcpsocket_accept_options);
                if (MEDUSA_IS_ERR_OR_NULL(accepted)) {
                        return MEDUSA_PTR_ERR(accepted);
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
        struct test_context test;
        struct medusa_tcpsocket *tcpsocket;
        struct medusa_tcpsocket_bind_options tcpsocket_bind_options;
        struct medusa_tcpsocket_connect_options tcpsocket_connect_options;

        monitor = NULL;
        memset(&test, 0, sizeof(struct test_context));

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
                tcpsocket_bind_options.backlog     = 128;
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
        tcpsocket_connect_options.nodelay     = 0;
        tcpsocket_connect_options.buffered    = 1;
        tcpsocket_connect_options.ssl         = 1;
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

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "  medusa_monitor_run failed\n");
                goto bail;
        }
        if (test.failed || test.echoed == 0 || test.ssl_connected != (1 | 2)) {
                fprintf(stderr, "  failed: %d, echoed: %d, ssl_connected: %d\n", test.failed, test.echoed, test.ssl_connected);
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

        SSL_library_init();
        SSL_load_error_strings();

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

#else

#include <stdio.h>

int main (int argc, char *argv[])
{
        (void) argc;
        (void) argv;
        fprintf(stderr, "medusa tcpsocket openssl support is disabled\n");
        return 0;
}

#endif
