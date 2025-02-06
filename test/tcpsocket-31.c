
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(MEDUSA_TEST_TCPSOCKET_SSL) && (MEDUSA_TEST_TCPSOCKET_SSL == 1)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "medusa/error.h"
#include "medusa/buffer.h"
#include "medusa/tcpsocket.h"
#include "medusa/monitor.h"

#define MAX(a, b)               (((a) > (b)) ? (a) : (b))

#define GREETING_MESSAGE        "greetings from server"

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
        void *buffer;
        int64_t length;

        (void) context;
        (void) param;

        fprintf(stderr, "client   events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));

        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                fprintf(stderr, "         - reading greeting message\n");
                length = medusa_buffer_get_length(medusa_tcpsocket_get_read_buffer(tcpsocket));
                if (length < 0) {
                        fprintf(stderr, "can not get tcpsocket read buffer length\n");
                        goto bail;
                }
                if (length == (strlen(GREETING_MESSAGE) + 1)) {
                        fprintf(stderr, "         - read whole greeting message\n");
                        buffer = medusa_buffer_linearize(medusa_tcpsocket_get_read_buffer(tcpsocket), 0, length);
                        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                                fprintf(stderr, "can not linearize tcpsocket read buffer\n");
                                goto bail;
                        }
                        if (memcmp(buffer, GREETING_MESSAGE, length) != 0) {
                                fprintf(stderr, "invalid data in tcpsocket read buffer\n");
                                goto bail;
                        } else {
                                fprintf(stderr, "         - greeting message is valid\n");
                                medusa_monitor_break(medusa_tcpsocket_get_monitor(tcpsocket));
                        }
                }
        }
        return 0;
bail:   return -1;
}

static int tcpsocket_server_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        (void) context;
        fprintf(stderr, "server   events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                fprintf(stderr, "         - writing greeting message\n");
                rc = medusa_buffer_write(medusa_tcpsocket_get_write_buffer(tcpsocket), GREETING_MESSAGE, strlen(GREETING_MESSAGE) + 1);
                if (rc != strlen(GREETING_MESSAGE) + 1) {
                        fprintf(stderr, "can not write to tcpsocket buffer (rc: %d)\n", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE) {
                struct medusa_tcpsocket_event_buffered_write *event_buffered_write = (struct medusa_tcpsocket_event_buffered_write *) param;
                fprintf(stderr, "  written: %"PRIi64"\n", event_buffered_write->length);
        }
        return 0;
bail:   return -1;
}

static int tcpsocket_listener_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        struct medusa_tcpsocket *accepted;
        struct medusa_tcpsocket_accept_options accepted_options;

        (void) context;
        (void) param;

        fprintf(stderr, "listener events: 0x%08x, %s\n", events, medusa_tcpsocket_event_string(events));
        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTION) {
                fprintf(stderr, "         - accepting new connection\n");
                rc = medusa_tcpsocket_accept_options_default(&accepted_options);
                if (rc != 0) {
                        fprintf(stderr, "can not init accept options\n");
                        goto bail;
                }
                accepted_options.buffered    = 1;
                accepted_options.nodelay     = 1;
                accepted_options.nonblocking = 1;
                accepted_options.enabled     = 1;
                accepted_options.onevent     = tcpsocket_server_onevent;
                accepted_options.context     = NULL;
                accepted = medusa_tcpsocket_accept_with_options(tcpsocket, &accepted_options);
                if (MEDUSA_IS_ERR_OR_NULL(accepted)) {
                        return MEDUSA_PTR_ERR(accepted);
                }
        }

        return 0;
bail:   return -1;
}

static int test_poll (unsigned int poll)
{
        int rc;

        int bind_fd;
        unsigned short bind_port;

        int connect_fd;
        unsigned short connect_port;

        int val;
        struct sockaddr_in sockaddr_in;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options monitor_init_options;

        struct medusa_tcpsocket *tcpsocket;
        struct medusa_tcpsocket_attach_options tcpsocket_attach_options;
        struct medusa_tcpsocket_connect_options tcpsocket_connect_options;

        bind_fd    = -1;
        connect_fd = -1;
        monitor    = NULL;

#if defined(MEDUSA_TEST_TCPSOCKET_SSL) && (MEDUSA_TEST_TCPSOCKET_SSL == 1)
        SSL_library_init();
        SSL_load_error_strings();
#endif

        rc = medusa_monitor_init_options_default(&monitor_init_options);
        if (rc != 0) {
                fprintf(stderr, "can not init monitor init options\n");
                goto bail;
        }
        monitor_init_options.poll.type = poll;
        monitor = medusa_monitor_create_with_options(&monitor_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "can not create monitor\n");
                goto bail;
        }

        fprintf(stderr, "creating bind\n");

        bind_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (bind_fd < 0) {
                fprintf(stderr, "can not open tcp socket\n");
                goto bail;
        }

        {
                socklen_t sockaddr_length;
                struct sockaddr_storage sockaddr_storage;

                char sockaddr_address[MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)];
                unsigned short sockaddr_port;

                sockaddr_length = sizeof(struct sockaddr_storage);
                memset(&sockaddr_storage, 0, sockaddr_length);
                rc = getsockname(bind_fd, (struct sockaddr *) &sockaddr_storage, &sockaddr_length);
                if (rc != 0) {
                        fprintf(stderr, "getsockname failed\n");
                        goto bail;
                }
                if (sockaddr_storage.ss_family == AF_INET) {
                        if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in *) &sockaddr_storage)->sin_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                fprintf(stderr, "can not get address from sockaddr\n");
                                goto bail;
                        }
                        sockaddr_port = ntohs(((struct sockaddr_in *) &sockaddr_storage)->sin_port);
                } else if (sockaddr_storage.ss_family == AF_INET6) {
                        if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                fprintf(stderr, "can not get address from sockaddr\n");
                                goto bail;
                        }
                        sockaddr_port = ntohs(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_port);
                } else {
                        fprintf(stderr, "sockaddr family is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "sockaddr: %s:%d\n", sockaddr_address, sockaddr_port);
        }

        val = 1;
        rc = setsockopt(bind_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
        if (rc != 0) {
                fprintf(stderr, "can not set reuseaddr on tcp socket\n");
                goto bail;
        }

        for (bind_port = 12345; bind_port < 65535; bind_port++) {
                sockaddr_in.sin_family = AF_INET;
                sockaddr_in.sin_port = htons(bind_port);
                sockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);
                fprintf(stderr, "check port to: %d\n", bind_port);
                rc = bind(bind_fd, (struct sockaddr *) &sockaddr_in, sizeof(sockaddr_in));
                if (rc == 0) {
                        break;
                }
        }
        if (bind_port >= 65535) {
                fprintf(stderr, "can not bind tcp socket\n");
                goto bail;
        }

        rc = listen(bind_fd, 128);
        if (rc < 0) {
                fprintf(stderr, "listen failed\n");
        }

        {
                socklen_t sockaddr_length;
                struct sockaddr_storage sockaddr_storage;

                char sockaddr_address[MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)];
                unsigned short sockaddr_port;

                sockaddr_length = sizeof(struct sockaddr_storage);
                memset(&sockaddr_storage, 0, sockaddr_length);
                rc = getsockname(bind_fd, (struct sockaddr *) &sockaddr_storage, &sockaddr_length);
                if (rc != 0) {
                        fprintf(stderr, "getsockname failed\n");
                        goto bail;
                }
                if (sockaddr_storage.ss_family == AF_INET) {
                        if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in *) &sockaddr_storage)->sin_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                fprintf(stderr, "can not get address from sockaddr\n");
                                goto bail;
                        }
                        sockaddr_port = ntohs(((struct sockaddr_in *) &sockaddr_storage)->sin_port);
                } else if (sockaddr_storage.ss_family == AF_INET6) {
                        if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                fprintf(stderr, "can not get address from sockaddr\n");
                                goto bail;
                        }
                        sockaddr_port = ntohs(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_port);
                } else {
                        fprintf(stderr, "sockaddr family is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "sockaddr: %s:%d\n", sockaddr_address, sockaddr_port);
        }

        rc = medusa_tcpsocket_attach_options_default(&tcpsocket_attach_options);
        if (rc != 0) {
                fprintf(stderr, "can not init tcpsocket attach options\n");
                goto bail;
        }
        tcpsocket_attach_options.monitor     = monitor;
        tcpsocket_attach_options.onevent     = tcpsocket_listener_onevent;
        tcpsocket_attach_options.context     = NULL;
        tcpsocket_attach_options.fd          = bind_fd;
        tcpsocket_attach_options.bound       = 1;
        tcpsocket_attach_options.clodestroy  = 1;
        tcpsocket_attach_options.nonblocking = 1;
        tcpsocket_attach_options.nodelay     = 1;
        tcpsocket_attach_options.buffered    = 1;
        tcpsocket_attach_options.enabled     = 1;
        tcpsocket = medusa_tcpsocket_attach_with_options(&tcpsocket_attach_options);
        if (MEDUSA_IS_ERR_OR_NULL(tcpsocket)) {
                fprintf(stderr, "medusa_tcpsocket_attach_with_options failed\n");
                goto bail;
        }

        fprintf(stderr, "bind_fd: %d, bind_port: %d\n", bind_fd, bind_port);

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

        fprintf(stderr, "creating connect\n");

        connect_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (connect_fd < 0) {
                fprintf(stderr, "can not open tcp socket\n");
                goto bail;
        }

        {
                socklen_t sockaddr_length;
                struct sockaddr_storage sockaddr_storage;

                char sockaddr_address[MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)];
                unsigned short sockaddr_port;

                sockaddr_length = sizeof(struct sockaddr_storage);
                memset(&sockaddr_storage, 0, sockaddr_length);
                rc = getsockname(connect_fd, (struct sockaddr *) &sockaddr_storage, &sockaddr_length);
                if (rc != 0) {
                        fprintf(stderr, "getsockname failed\n");
                        goto bail;
                }
                if (sockaddr_storage.ss_family == AF_INET) {
                        if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in *) &sockaddr_storage)->sin_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                fprintf(stderr, "can not get address from sockaddr\n");
                                goto bail;
                        }
                        sockaddr_port = ntohs(((struct sockaddr_in *) &sockaddr_storage)->sin_port);
                } else if (sockaddr_storage.ss_family == AF_INET6) {
                        if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                fprintf(stderr, "can not get address from sockaddr\n");
                                goto bail;
                        }
                        sockaddr_port = ntohs(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_port);
                } else {
                        fprintf(stderr, "sockaddr family is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "sockaddr: %s:%d\n", sockaddr_address, sockaddr_port);
        }

        for (connect_port = 12345; connect_port < 65535; connect_port++) {
                sockaddr_in.sin_family = AF_INET;
                sockaddr_in.sin_port = htons(connect_port);
                sockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);
                fprintf(stderr, "check port to: %d\n", connect_port);
                rc = bind(connect_fd, (struct sockaddr *) &sockaddr_in, sizeof(sockaddr_in));
                if (rc == 0) {
                        break;
                }
        }
        if (connect_port >= 65535) {
                fprintf(stderr, "can not bind tcp socket\n");
                goto bail;
        }

        fprintf(stderr, "connect_fd: %d, connect_port: %d\n", connect_fd, connect_port);

        {
                socklen_t sockaddr_length;
                struct sockaddr_storage sockaddr_storage;

                char sockaddr_address[MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)];
                unsigned short sockaddr_port;

                sockaddr_length = sizeof(struct sockaddr_storage);
                memset(&sockaddr_storage, 0, sockaddr_length);
                rc = getsockname(connect_fd, (struct sockaddr *) &sockaddr_storage, &sockaddr_length);
                if (rc != 0) {
                        fprintf(stderr, "getsockname failed\n");
                        goto bail;
                }
                if (sockaddr_storage.ss_family == AF_INET) {
                        if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in *) &sockaddr_storage)->sin_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                fprintf(stderr, "can not get address from sockaddr\n");
                                goto bail;
                        }
                        sockaddr_port = ntohs(((struct sockaddr_in *) &sockaddr_storage)->sin_port);
                } else if (sockaddr_storage.ss_family == AF_INET6) {
                        if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                fprintf(stderr, "can not get address from sockaddr\n");
                                goto bail;
                        }
                        sockaddr_port = ntohs(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_port);
                } else {
                        fprintf(stderr, "sockaddr family is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "sockaddr: %s:%d\n", sockaddr_address, sockaddr_port);
        }

        rc = medusa_tcpsocket_connect_options_default(&tcpsocket_connect_options);
        if (rc < 0) {
                fprintf(stderr, "medusa_tcpsocket_connect_options_default failed\n");
                goto bail;
        }
        tcpsocket_connect_options.monitor     = monitor;
        tcpsocket_connect_options.onevent     = tcpsocket_client_onevent;
        tcpsocket_connect_options.context     = NULL;
        tcpsocket_connect_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
        tcpsocket_connect_options.address     = "127.0.0.1";
        tcpsocket_connect_options.port        = bind_port;
        tcpsocket_connect_options.fd          = connect_fd;
        tcpsocket_connect_options.clodestroy  = 0;
        tcpsocket_connect_options.nonblocking = 1;
        tcpsocket_connect_options.nodelay     = 1;
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

        close(bind_fd);
        close(connect_fd);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        if (bind_fd >= 0) {
                close(bind_fd);
        }
        if (connect_fd >= 0) {
                close(connect_fd);
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
