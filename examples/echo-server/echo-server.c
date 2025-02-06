
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>

#if defined(__WINDOWS__)
#include <winsock2.h>
#include <wspiapi.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#if defined(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) && (MEDUSA_TCPSOCKET_OPENSSL_ENABLE == 1)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "medusa/error.h"
#include "medusa/iovec.h"
#include "medusa/buffer.h"
#include "medusa/io.h"
#include "medusa/tcpsocket.h"
#include "medusa/udpsocket.h"
#include "medusa/signal.h"
#include "medusa/monitor.h"

#define MAX(a, b)               (((a) > (b)) ? (a) : (b))

static int g_running;
static int g_verbose;
static int g_echo;
static unsigned char g_udpbuffer[65536];

#define OPTION_PROTOCOL_DEFAULT         "tcp"
#define OPTION_ADDRESS_DEFAULT          "0.0.0.0"
#define OPTION_PORT_DEFAULT             12345
#define OPTION_SSL_DEFAULT              0
#define OPTION_SSL_CERTIFICATE_DEFAULT  "certificate.crt"
#define OPTION_SSL_PRIVATEKEY_DEFAULT   "privatekey.key"
#define OPTION_ECHO_DEFAULT             1
#define OPTION_VERBOSE_DEFAULT          0

#define OPTION_HELP                     'h'
#define OPTION_PROTOCOL                 'r'
#define OPTION_ADDRESS                  'a'
#define OPTION_PORT                     'p'
#define OPTION_SSL                      'S'
#define OPTION_SSL_CERTIFICATE          'C'
#define OPTION_SSL_PRIVATEKEY           'K'
#define OPTION_ECHO                     'e'
#define OPTION_VERBOSE                  'v'

static struct option longopts[] = {
        { "help",               no_argument,            NULL,   OPTION_HELP             },
        { "protocol",           required_argument,      NULL,   OPTION_PROTOCOL         },
        { "address",            required_argument,      NULL,   OPTION_ADDRESS          },
        { "port",               required_argument,      NULL,   OPTION_PORT             },
        { "ssl",                required_argument,      NULL,   OPTION_SSL              },
        { "ssl_certificate",    required_argument,      NULL,   OPTION_SSL_CERTIFICATE  },
        { "ssl_privatekey",     required_argument,      NULL,   OPTION_SSL_PRIVATEKEY   },
        { "echo",               required_argument,      NULL,   OPTION_ECHO             },
        { "verbose",            required_argument,      NULL,   OPTION_VERBOSE          },
        { NULL,                 0,                      NULL,   0                       }
};

static void usage (const char *pname)
{
        fprintf(stdout, "usage: %s [-P protocol] [-a address] [-p port] [-s ssl] [-c certificate.crt] [-k privatekey.key] [-v level]\n", pname);
        fprintf(stdout, "  -h. --help   : this text\n");
        fprintf(stdout, "  -v, --verbose : verbose level (default: %d)\n", OPTION_VERBOSE_DEFAULT);
        fprintf(stdout, "  -e, --echo    : echo back (default: %d)\n", OPTION_ECHO_DEFAULT);
        fprintf(stdout, "  -r, --protocol: listening protocol (values: tcp, udp, default: %s)\n", OPTION_PROTOCOL_DEFAULT);
        fprintf(stdout, "  -a, --address : listening address (values: interface ip address, default: %s)\n", OPTION_ADDRESS_DEFAULT);
        fprintf(stdout, "  -p. --port    : listening port (values: 0 < port < 65536, default: %d)\n", OPTION_PORT_DEFAULT);
        fprintf(stdout, "  -S, --ssl            : enable ssl (default: %d)\n", OPTION_SSL_DEFAULT);
        fprintf(stdout, "  -C, --ssl_certificate: ssl certificate (default: %s)\n", OPTION_SSL_CERTIFICATE_DEFAULT);
        fprintf(stdout, "  -K, --ssl_privatekey : ssl privatekey (default: %s)\n", OPTION_SSL_PRIVATEKEY_DEFAULT);
}

#define verbosef(level, tag, fmt...) {                                                  \
        if (g_verbose >= level) {                                                       \
                fprintf(stderr, "verbose:%d:%s: ", level, tag);                         \
                fprintf(stderr, fmt);                                                   \
                fprintf(stderr, " (%s %s:%d)\n", __FUNCTION__, __FILE__, __LINE__);     \
                fflush(stderr);                                                         \
        }                                                                               \
}

#define errorf(fmt...)          verbosef(0, "error  ", fmt);
#define warningf(fmt...)        verbosef(1, "warning", fmt);
#define noticef(fmt...)         verbosef(2, "notice ", fmt);
#define debugf(fmt...)          verbosef(3, "debug  ", fmt);
#define tracef(fmt...)          verbosef(4, "trace  ", fmt);

static int client_medusa_tcpsocket_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        int64_t rlen;
        int64_t wlen;
        struct medusa_buffer *rbuffer;
        struct medusa_buffer *wbuffer;
        int64_t i;
        int64_t niovecs;
        struct medusa_iovec iovecs[16];

        (void) context;
        (void) param;

        debugf("tcpsocket events: 0x%08x, %s", events, medusa_tcpsocket_event_string(events));

        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                rbuffer = medusa_tcpsocket_get_read_buffer(tcpsocket);
                if (rbuffer == NULL) {
                        return MEDUSA_PTR_ERR(rbuffer);
                }
                niovecs = medusa_buffer_peekv(rbuffer, 0, -1, iovecs, 16);
                if (niovecs < 0) {
                        errorf("medusa_buffer_peekv failed");
                        return niovecs;
                }
                for (rlen = 0, i = 0; i < niovecs; i++) {
                        rlen += iovecs[i].iov_len;
                }
                wbuffer = medusa_tcpsocket_get_write_buffer(tcpsocket);
                if (wbuffer == NULL) {
                        errorf("medusa_tcpsocket_get_write_buffer failed");
                        return MEDUSA_PTR_ERR(wbuffer);
                }
                wlen = medusa_buffer_appendv(wbuffer, iovecs, niovecs);
                if (wlen < 0) {
                        errorf("medusa_buffer_appendv failed");
                        return wlen;
                }
                if (wlen != rlen) {
                        errorf("medusa_buffer_appendv failed");
                        return -EIO;
                }
                char *wdata;
                wdata = medusa_buffer_linearize(rbuffer, 0, wlen);
                fprintf(stdout, "%.*s", (int) wlen, wdata);
                fflush(stdout);
                rc = medusa_buffer_choke(rbuffer, 0, wlen);
                if (rc < 0) {
                        errorf("medusa_buffer_choke failed");
                        return rc;
                }
        }
        return 0;
}

static int listener_medusa_tcpsocket_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        struct medusa_tcpsocket *medusa_tcpsocket;
        struct medusa_tcpsocket_accept_options medusa_tcpsocket_accept_options;

        (void) context;
        (void) param;

        debugf("tcpsocket events: 0x%08x, %s", events, medusa_tcpsocket_event_string(events));

        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTION) {
                rc = medusa_tcpsocket_accept_options_default(&medusa_tcpsocket_accept_options);
                if (rc < 0) {
                        errorf("medusa_tcpsocket_accept_options_default failed");
                        return rc;
                }
                medusa_tcpsocket_accept_options.onevent     = client_medusa_tcpsocket_onevent;
                medusa_tcpsocket_accept_options.context     = NULL;
                medusa_tcpsocket_accept_options.nonblocking = 1;
                medusa_tcpsocket_accept_options.enabled     = 1;
                medusa_tcpsocket_accept_options.buffered    = 1;
                medusa_tcpsocket = medusa_tcpsocket_accept_with_options(tcpsocket, &medusa_tcpsocket_accept_options);
                if (MEDUSA_IS_ERR_OR_NULL(medusa_tcpsocket)) {
                        errorf("medusa_tcpsocket_accept_with_options failed");
                        return MEDUSA_PTR_ERR(medusa_tcpsocket);
                }
        }
        return 0;
}

static int listener_medusa_udpsocket_onevent (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param)
{
        ssize_t rc;
        struct sockaddr_storage sockaddr_storage;
        socklen_t sockaddr_length = sizeof(struct sockaddr_storage);

        (void) udpsocket;
        (void) events;
        (void) context;
        (void) param;

        debugf("udpsocket events: 0x%08x, %s", events, medusa_udpsocket_event_string(events));

        if (events & MEDUSA_UDPSOCKET_EVENT_IN) {
                rc = recvfrom(medusa_udpsocket_get_fd(udpsocket), (void *) g_udpbuffer, sizeof(g_udpbuffer), 0, (struct sockaddr *) &sockaddr_storage, &sockaddr_length);
                if (rc < 0) {
#if defined(__WINDOWS__)
                        if (rc == SOCKET_ERROR) {
                                errno = WSAGetLastError();
                                if (errno == WSAECONNRESET) {
                                        errno = ECONNRESET;
                                }
                        }
#endif
                        errorf("can not recv from medusa udpsocket (rc: %d, errno: %d, %s)", (int) rc, errno, strerror(errno));
                        goto bail;
                }
                {
                        char sockaddr_address[MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)];
                        unsigned short sockaddr_port;

                        if (sockaddr_storage.ss_family == AF_INET) {
                                if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in *) &sockaddr_storage)->sin_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                        errorf("can not get address from sockaddr");
                                        goto bail;
                                }
                                sockaddr_port = ntohs(((struct sockaddr_in *) &sockaddr_storage)->sin_port);
                        } else if (sockaddr_storage.ss_family == AF_INET6) {
                                if (inet_ntop(sockaddr_storage.ss_family, &(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_addr), sockaddr_address, sizeof(sockaddr_address)) == NULL) {
                                        errorf("can not get address from sockaddr");
                                        goto bail;
                                }
                                sockaddr_port = ntohs(((struct sockaddr_in6 *) &sockaddr_storage)->sin6_port);
                        } else {
                                errorf("sockaddr family is invalid");
                                goto bail;
                        }
                        debugf("%d bytes received from udp://%s:%d", (int) rc, sockaddr_address, sockaddr_port);
                }
                if (g_echo) {
                        rc = sendto(medusa_udpsocket_get_fd(udpsocket), (void *) g_udpbuffer, rc, 0, (struct sockaddr *) &sockaddr_storage, sockaddr_length);
                        if (rc < 0) {
                                errorf("can not recv from medusa udpsocket (rc: %d, errno: %d, %s)", (int) rc, errno, strerror(errno));
                                goto bail;
                        }
                }
        }

        return 0;
bail:   return -1;
}

static int sigint_medusa_signal_onevent (struct medusa_signal *signal, unsigned int events, void *context, void *param)
{
        (void) signal;
        (void) events;
        (void) context;
        (void) param;
        g_running = 0;
        return medusa_monitor_break(medusa_signal_get_monitor(signal));
}

int main (int argc, char *argv[])
{
        int rc;
        int err;

        int c;
        int option_port;
        const char *option_address;
        const char *option_protocol;

        int option_ssl;
        const char *option_ssl_certificate;
        const char *option_ssl_privatekey;

        struct medusa_tcpsocket *medusa_tcpsocket;
        struct medusa_tcpsocket_bind_options medusa_tcpsocket_bind_options;

        struct medusa_udpsocket *medusa_udpsocket;
        struct medusa_udpsocket_bind_options medusa_udpsocket_bind_options;

        struct medusa_signal *medusa_signal;
        struct medusa_signal_init_options medusa_signal_init_options;

        struct medusa_monitor *medusa_monitor;
        struct medusa_monitor_init_options medusa_monitor_init_options;

#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

#if defined(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) && (MEDUSA_TCPSOCKET_OPENSSL_ENABLE == 1)
        SSL_library_init();
        SSL_load_error_strings();
#endif

        err             = 0;
        medusa_monitor  = NULL;

        option_port     = OPTION_PORT_DEFAULT;
        option_address  = OPTION_ADDRESS_DEFAULT;
        option_protocol = OPTION_PROTOCOL_DEFAULT;

        option_ssl              = OPTION_SSL_DEFAULT;
        option_ssl_certificate  = OPTION_SSL_CERTIFICATE_DEFAULT;
        option_ssl_privatekey   = OPTION_SSL_PRIVATEKEY_DEFAULT;

        g_echo    = OPTION_ECHO_DEFAULT;
        g_verbose = OPTION_VERBOSE_DEFAULT;

        g_running = 1;

        while ((c = getopt_long(argc, argv, "hr:a:p:S:C:K:e:v:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HELP:
                                usage(argv[0]);
                                goto out;
                        case OPTION_PROTOCOL:
                                option_protocol = optarg;
                                break;
                        case OPTION_ADDRESS:
                                option_address = optarg;
                                break;
                        case OPTION_PORT:
                                option_port = atoi(optarg);
                                break;
                        case OPTION_SSL:
                                option_ssl = !!atoi(optarg);
                                break;
                        case OPTION_SSL_CERTIFICATE:
                                option_ssl_certificate = optarg;
                                break;
                        case OPTION_SSL_PRIVATEKEY:
                                option_ssl_privatekey = optarg;
                                break;
                        case OPTION_ECHO:
                                g_echo = atoi(optarg);
                                break;
                        case OPTION_VERBOSE:
                                g_verbose = atoi(optarg);
                                break;
                        default:
                                errorf("unknown option: %d", optopt);
                                err = -EINVAL;
                                goto out;
                }
        }

        rc = medusa_monitor_init_options_default(&medusa_monitor_init_options);
        if (rc < 0) {
                err = rc;
                goto out;
        }
        medusa_monitor = medusa_monitor_create_with_options(&medusa_monitor_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(medusa_monitor)) {
                err = MEDUSA_PTR_ERR(medusa_monitor);
                goto out;
        }

        rc = medusa_signal_init_options_default(&medusa_signal_init_options);
        if (rc < 0) {
                err = rc;
                goto out;
        }
        medusa_signal_init_options.number     = SIGINT;
        medusa_signal_init_options.onevent    = sigint_medusa_signal_onevent;
        medusa_signal_init_options.context    = NULL;
        medusa_signal_init_options.singleshot = 0;
        medusa_signal_init_options.enabled    = 1;
        medusa_signal_init_options.monitor    = medusa_monitor;
        medusa_signal = medusa_signal_create_with_options(&medusa_signal_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(medusa_signal)) {
                err = MEDUSA_PTR_ERR(medusa_signal);
                goto out;
        }

        debugf("listening from %s://%s:%d", option_protocol, option_address, option_port);

        if (strcasecmp(option_protocol, "t") == 0 || strcasecmp(option_protocol, "tcp") == 0) {
                rc = medusa_tcpsocket_bind_options_default(&medusa_tcpsocket_bind_options);
                if (rc < 0) {
                        err = rc;
                        goto out;
                }
                medusa_tcpsocket_bind_options.monitor     = medusa_monitor;
                medusa_tcpsocket_bind_options.onevent     = listener_medusa_tcpsocket_onevent;
                medusa_tcpsocket_bind_options.context     = NULL;
                medusa_tcpsocket_bind_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
                medusa_tcpsocket_bind_options.address     = option_address;
                medusa_tcpsocket_bind_options.port        = option_port;
                medusa_tcpsocket_bind_options.nonblocking = 1;
                medusa_tcpsocket_bind_options.reuseaddr   = 1;
                medusa_tcpsocket_bind_options.reuseport   = 1;
                medusa_tcpsocket_bind_options.buffered    = !!option_ssl;
                medusa_tcpsocket_bind_options.backlog     = 128;
                medusa_tcpsocket_bind_options.enabled     = 1;
                medusa_tcpsocket = medusa_tcpsocket_bind_with_options(&medusa_tcpsocket_bind_options);
                if (MEDUSA_IS_ERR_OR_NULL(medusa_tcpsocket)) {
                        errorf("medusa_tcpsocket_bind_with_options failed");
                        err = MEDUSA_PTR_ERR(medusa_tcpsocket);
                        goto out;
                }

                if (option_ssl == 1) {
                        rc = medusa_tcpsocket_set_ssl_certificate_file(medusa_tcpsocket, option_ssl_certificate);
                        if (rc < 0) {
                                errorf("medusa_tcpsocket_set_ssl_certificate_file failed");
                                err = rc;
                                goto out;
                        }
                        rc = medusa_tcpsocket_set_ssl_privatekey_file(medusa_tcpsocket, option_ssl_privatekey);
                        if (rc < 0) {
                                errorf("medusa_tcpsocket_set_ssl_privatekey_file failed");
                                err = rc;
                                goto out;
                        }
                }
                rc = medusa_tcpsocket_set_ssl(medusa_tcpsocket, option_ssl);
                if (rc < 0) {
                        errorf("medusa_tcpsocket_set_ssl failed");
                        err = rc;
                        goto out;
                }
        } else if (strcasecmp(option_protocol, "u") == 0 || strcasecmp(option_protocol, "udp") == 0) {
                rc = medusa_udpsocket_bind_options_default(&medusa_udpsocket_bind_options);
                if (rc < 0) {
                        err = rc;
                        goto out;
                }
                medusa_udpsocket_bind_options.monitor     = medusa_monitor;
                medusa_udpsocket_bind_options.onevent     = listener_medusa_udpsocket_onevent;
                medusa_udpsocket_bind_options.context     = NULL;
                medusa_udpsocket_bind_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
                medusa_udpsocket_bind_options.address     = option_address;
                medusa_udpsocket_bind_options.port        = option_port;
                medusa_udpsocket_bind_options.nonblocking = 1;
                medusa_udpsocket_bind_options.reuseaddr   = 1;
                medusa_udpsocket_bind_options.reuseport   = 1;
                medusa_udpsocket_bind_options.enabled     = 1;
                medusa_udpsocket = medusa_udpsocket_bind_with_options(&medusa_udpsocket_bind_options);
                if (MEDUSA_IS_ERR_OR_NULL(medusa_udpsocket)) {
                        err = MEDUSA_PTR_ERR(medusa_udpsocket);
                        goto out;
                }
        } else {
                errorf("option_protocol: %s is invalid", option_protocol);
                err = -EINVAL;
                goto out;
        }

        while (g_running == 1) {
                debugf("loop");
                rc = medusa_monitor_run_once(medusa_monitor);
                if (rc < 0) {
                        errorf("medusa_monitor_run_once failed");
                        err = rc;
                        goto out;
                }
        }

out:    if (!MEDUSA_IS_ERR_OR_NULL(medusa_monitor)) {
                medusa_monitor_destroy(medusa_monitor);
        }
        return err;
}
