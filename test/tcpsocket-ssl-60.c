
#if defined(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) && (MEDUSA_TCPSOCKET_OPENSSL_ENABLE ==1)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "watchdog.h"

#include "medusa/error.h"
#include "medusa/tcpsocket.h"
#include "medusa/monitor.h"

static const unsigned int g_polls[] = {
        MEDUSA_MONITOR_POLL_DEFAULT,
#if defined(__LINUX__)
        MEDUSA_MONITOR_POLL_EPOLL,
        MEDUSA_MONITOR_POLL_POLL,
#endif
#if defined(__APPLE__)
        MEDUSA_MONITOR_POLL_KQUEUE,
        MEDUSA_MONITOR_POLL_POLL,
#endif
        MEDUSA_MONITOR_POLL_SELECT,
#if defined(__WINDOWS__)
        MEDUSA_MONITOR_POLL_WSAPOLL,
        MEDUSA_MONITOR_POLL_IOCP
#endif
};

struct test_alpn {
        const char *server;
        const char *client;
        const char *selected;
        int fails;
};

static const struct test_alpn g_alpns[] = {
        { "h2,http/1.1",        "http/1.1,h2",          "h2",           0 },
        { "http/1.1",           "h2,http/1.1",          "http/1.1",     0 },
        { " h2 , http/1.1 ",    "http/1.1",             "http/1.1",     0 },
        { NULL,                 "h2,http/1.1",          NULL,           0 },
        { "h2,http/1.1",        NULL,                   NULL,           0 },
        { NULL,                 NULL,                   NULL,           0 },
        { "http/1.1",           "h2",                   NULL,           1 },
};

struct test_context {
        const struct test_alpn *alpn;
        unsigned int done;
        int failed;
};

static int test_alpn_check (const char *name, const char *selected, const char *expected)
{
        fprintf(stderr, "%s selected: %s, expected: %s\n", name, (selected != NULL) ? selected : "(null)", (expected != NULL) ? expected : "(null)");
        if (MEDUSA_IS_ERR(selected)) {
                return -1;
        }
        if (selected == NULL && expected == NULL) {
                return 0;
        }
        if (selected == NULL || expected == NULL) {
                return -1;
        }
        if (strcmp(selected, expected) != 0) {
                return -1;
        }
        return 0;
}

static int tcpsocket_client_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        struct test_context *test = context;
        (void) param;
        fprintf(stderr, "client   events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED_SSL) {
                if (test->alpn->fails) {
                        test->failed = 1;
                        return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                }
                if (test_alpn_check("client", medusa_tcpsocket_get_ssl_alpn_selected(tcpsocket), test->alpn->selected) != 0) {
                        test->failed = 1;
                        return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                }
                test->done |= 1;
                if (test->done == (1 | 2)) {
                        return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                }
        }
        if (events & (MEDUSA_TCPSOCKET_EVENT_ERROR | MEDUSA_TCPSOCKET_EVENT_DISCONNECTED)) {
                if (!test->alpn->fails) {
                        test->failed = 1;
                }
                if (medusa_tcpsocket_get_ssl_alpn_selected(tcpsocket) != NULL) {
                        test->failed = 1;
                }
                test->done |= (1 | 2);
                return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
        }
        return 0;
}

static int tcpsocket_server_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        struct test_context *test = context;
        (void) param;
        fprintf(stderr, "server   events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED_SSL) {
                if (test->alpn->fails) {
                        test->failed = 1;
                        return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                }
                if (test_alpn_check("server", medusa_tcpsocket_get_ssl_alpn_selected(tcpsocket), test->alpn->selected) != 0) {
                        test->failed = 1;
                        return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                }
                test->done |= 2;
                if (test->done == (1 | 2)) {
                        return medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                }
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

static int test_poll (unsigned int poll, const struct test_alpn *alpn)
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
        test.alpn = alpn;

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
                tcpsocket_bind_options.ssl_alpn    = alpn->server;
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

        if (alpn->server != NULL && strcmp(medusa_tcpsocket_get_ssl_alpn(tcpsocket), alpn->server) != 0) {
                fprintf(stderr, "medusa_tcpsocket_get_ssl_alpn mismatch\n");
                goto bail;
        }
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
        tcpsocket_connect_options.ssl_alpn    = alpn->client;
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
        if (test.failed || test.done != (1 | 2)) {
                fprintf(stderr, "  alpn server: %s, client: %s failed\n", (alpn->server != NULL) ? alpn->server : "(null)", (alpn->client != NULL) ? alpn->client : "(null)");
                goto bail;
        }

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

static int tcpsocket_parse_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        (void) tcpsocket;
        (void) events;
        (void) context;
        (void) param;
        return 0;
}

static int test_alpn_parse (void)
{
        int rc;
        unsigned int i;
        struct medusa_monitor *monitor;
        struct medusa_tcpsocket *tcpsocket;
        struct medusa_tcpsocket_connect_options tcpsocket_connect_options;
        static const char *invalids[] = {
                "",
                ",",
                "h2,",
                ",h2",
                "h2,,http/1.1",
                " , h2",
                "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
                "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
                "01234567890123456789012345678901234567890123456789012345",
        };

        monitor = medusa_monitor_create();
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                goto bail;
        }
        medusa_tcpsocket_connect_options_default(&tcpsocket_connect_options);
        tcpsocket_connect_options.monitor     = monitor;
        tcpsocket_connect_options.onevent     = tcpsocket_parse_onevent;
        tcpsocket_connect_options.address     = "127.0.0.1";
        tcpsocket_connect_options.port        = 1;
        tcpsocket_connect_options.nonblocking = 1;
        tcpsocket_connect_options.buffered    = 1;
        tcpsocket_connect_options.enabled     = 0;
        tcpsocket = medusa_tcpsocket_connect_with_options(&tcpsocket_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(tcpsocket)) {
                fprintf(stderr, "medusa_tcpsocket_connect_with_options failed\n");
                goto bail;
        }
        for (i = 0; i < sizeof(invalids) / sizeof(invalids[0]); i++) {
                rc = medusa_tcpsocket_set_ssl_alpn(tcpsocket, invalids[i]);
                if (rc != -EINVAL) {
                        fprintf(stderr, "medusa_tcpsocket_set_ssl_alpn accepted invalid: '%s', rc: %d\n", invalids[i], rc);
                        goto bail;
                }
        }
        rc = medusa_tcpsocket_set_ssl_alpn(tcpsocket, "h2,http/1.1");
        if (rc != 0 || strcmp(medusa_tcpsocket_get_ssl_alpn(tcpsocket), "h2,http/1.1") != 0) {
                fprintf(stderr, "medusa_tcpsocket_set_ssl_alpn failed\n");
                goto bail;
        }
        rc = medusa_tcpsocket_set_ssl_alpn(tcpsocket, "h2,");
        if (rc != -EINVAL || strcmp(medusa_tcpsocket_get_ssl_alpn(tcpsocket), "h2,http/1.1") != 0) {
                fprintf(stderr, "medusa_tcpsocket_set_ssl_alpn did not keep previous list on error\n");
                goto bail;
        }
        rc = medusa_tcpsocket_set_ssl_alpn(tcpsocket, NULL);
        if (rc != 0 || medusa_tcpsocket_get_ssl_alpn(tcpsocket) != NULL) {
                fprintf(stderr, "medusa_tcpsocket_set_ssl_alpn could not clear\n");
                goto bail;
        }
        rc = medusa_tcpsocket_set_ssl_alpn(tcpsocket, "http/1.1");
        if (rc != 0) {
                fprintf(stderr, "medusa_tcpsocket_set_ssl_alpn failed\n");
                goto bail;
        }

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
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
        unsigned int j;

        (void) argc;
        (void) argv;

        srand(time(NULL));
        signal(SIGINT, sigint_handler);

        SSL_library_init();
        SSL_load_error_strings();

        watchdog_arm(5, NULL);
        rc = test_alpn_parse();
        if (rc != 0) {
                fprintf(stderr, "  failed\n");
                return -1;
        }

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                for (j = 0; j < sizeof(g_alpns) / sizeof(g_alpns[0]); j++) {
                        watchdog_arm(5, NULL);

                        fprintf(stderr, "testing poll: %d, alpn server: %s, client: %s\n", g_polls[i], (g_alpns[j].server != NULL) ? g_alpns[j].server : "(null)", (g_alpns[j].client != NULL) ? g_alpns[j].client : "(null)");
                        rc = test_poll(g_polls[i], &g_alpns[j]);
                        if (rc != 0) {
                                fprintf(stderr, "  failed\n");
                                return -1;
                        }
                        fprintf(stderr, "success\n");
                }
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
