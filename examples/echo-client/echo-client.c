
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>

#if defined(__WINDOWS__)
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
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

static int g_running;
static int g_use_iovec;
static int g_use_ssl;
static int g_verbose;

#define OPTION_PROTOCOL_DEFAULT         "tcp"
#define OPTION_ADDRESS_DEFAULT          "0.0.0.0"
#define OPTION_DPORT_DEFAULT            12345
#define OPTION_SPORT_DEFAULT            0
#define OPTION_STRING_DEFAULT           "hello from medusa echo client"
#define OPTION_IOVEC_DEFAULT            0
#define OPTION_SSL_DEFAULT              0
#define OPTION_VERBOSE_DEFAULT          0

#define OPTION_HELP                     'h'
#define OPTION_PROTOCOL                 'r'
#define OPTION_ADDRESS                  'a'
#define OPTION_DPORT                    'p'
#define OPTION_SPORT                    'P'
#define OPTION_STRING                   's'
#define OPTION_IOVEC                    'i'
#define OPTION_SSL                      'S'
#define OPTION_VERBOSE                  'v'
static struct option longopts[] = {
        { "help",               no_argument,            NULL,   OPTION_HELP     },
        { "protocol",           required_argument,      NULL,   OPTION_PROTOCOL },
        { "address",            required_argument,      NULL,   OPTION_ADDRESS  },
        { "dport",              required_argument,      NULL,   OPTION_DPORT    },
        { "sport",              required_argument,      NULL,   OPTION_SPORT    },
        { "string",             required_argument,      NULL,   OPTION_STRING   },
        { "iovec",              required_argument,      NULL,   OPTION_IOVEC    },
        { "ssl",                required_argument,      NULL,   OPTION_SSL      },
        { "verbose",            required_argument,      NULL,   OPTION_VERBOSE  },
        { NULL,                 0,                      NULL,   0               },
};

static void usage (const char *pname)
{
        fprintf(stdout, "usage: %s [option] [text]:\n", pname);
        fprintf(stdout, "  -h. --help    : this text\n");
        fprintf(stdout, "  -v, --verbose : verbose level (default: %d)\n", OPTION_VERBOSE_DEFAULT);
        fprintf(stdout, "  -r, --protocol: listening protocol (values: tcp, udp, default: %s)\n", OPTION_PROTOCOL_DEFAULT);
        fprintf(stdout, "  -a, --address : server address (default: %s)\n", OPTION_ADDRESS_DEFAULT);
        fprintf(stdout, "  -p. --dport   : destination port (default: %d)\n", OPTION_DPORT_DEFAULT);
        fprintf(stdout, "  -P. --sport   : source port (default: %d)\n", OPTION_SPORT_DEFAULT);
        fprintf(stdout, "  -s. --string  : string to send (default: %s)\n", OPTION_STRING_DEFAULT);
        fprintf(stdout, "  -i, --iovec   : use iovec read (default: %d)\n", OPTION_IOVEC_DEFAULT);
        fprintf(stdout, "  -S, --ssl     : enable ssl (default: %d)\n", OPTION_SSL_DEFAULT);
}

#define verbosef(level, fmt...) {                                                       \
        if (g_verbose >= level) {                                                       \
                fprintf(stderr, "verbose:%d: ", level);                                 \
                fprintf(stderr, fmt);                                                   \
                fprintf(stderr, " (%s %s:%d)\n", __FUNCTION__, __FILE__, __LINE__);     \
                fflush(stderr);                                                         \
        }                                                                               \
}

static int sender_medusa_tcpsocket_onevent (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        const char *option_string = context;

        (void) param;

        verbosef(1, "tcpsocket events: 0x%08x, %s", events, medusa_tcpsocket_event_string(events));

        if (events & MEDUSA_TCPSOCKET_EVENT_ERROR) {
                fprintf(stderr, "tcpsocket error: %d, %s\n", medusa_tcpsocket_get_error(tcpsocket), strerror(medusa_tcpsocket_get_error(tcpsocket)));
                return medusa_tcpsocket_get_error(tcpsocket);
        }

        if (events & MEDUSA_TCPSOCKET_EVENT_CONNECTED) {
                rc = medusa_tcpsocket_set_ssl(tcpsocket, g_use_ssl);
                if (rc < 0) {
                        fprintf(stderr, "can not set ssl\n");
                        return rc;
                }
        }

        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE) {
        }

        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_WRITE_FINISHED) {
        }

        if (events & MEDUSA_TCPSOCKET_EVENT_BUFFERED_READ) {
                if (g_use_iovec != 0) {
                        int64_t rlength;
                        struct medusa_buffer *rbuffer;

                        int64_t i;
                        int64_t niovecs;
                        struct medusa_iovec *iovecs;

                        rbuffer = medusa_tcpsocket_get_read_buffer(tcpsocket);
                        if (MEDUSA_IS_ERR_OR_NULL(rbuffer)) {
                                return MEDUSA_PTR_ERR(rbuffer);
                        }

                        rlength = medusa_buffer_get_length(rbuffer);
                        if (rlength < (int) strlen(option_string) + 1) {
                                return 0;
                        }
                        if (rlength > (int) strlen(option_string) + 1) {
                                return -EIO;
                        }

                        niovecs = medusa_buffer_peekv(rbuffer, 0, -1, NULL, 0);
                        if (niovecs < 0) {
                                return niovecs;
                        }
                        if (niovecs == 0) {
                                return -EIO;
                        }

                        iovecs = malloc(sizeof(struct medusa_iovec) * niovecs);
                        if (iovecs == NULL) {
                                return -ENOMEM;
                        }
                        niovecs = medusa_buffer_peekv(rbuffer, 0, -1, iovecs, niovecs);
                        if (niovecs < 0) {
                                free(iovecs);
                                return niovecs;
                        }
                        if (niovecs == 0) {
                                free(iovecs);
                                return -EIO;
                        }

                        rlength = 0;
                        for (i = 0; i < niovecs; i++) {
                                rlength += iovecs[i].iov_len;
                        }
                        if (rlength != (int) strlen(option_string) + 1) {
                                free(iovecs);
                                return -EIO;
                        }

                        rlength = 0;
                        for (i = 0; i < niovecs; i++) {
                                rc = memcmp(iovecs[i].iov_base, option_string + rlength, iovecs[i].iov_len);
                                if (rc != 0) {
                                        free(iovecs);
                                        return -EIO;
                                }
                                rlength += iovecs[i].iov_len;
                        }
                        free(iovecs);
                } else {
                        int64_t rlength;
                        struct medusa_buffer *rbuffer;

                        char *data;

                        rbuffer = medusa_tcpsocket_get_read_buffer(tcpsocket);
                        if (MEDUSA_IS_ERR_OR_NULL(rbuffer)) {
                                return MEDUSA_PTR_ERR(rbuffer);
                        }

                        rlength = medusa_buffer_get_length(rbuffer);
                        if (rlength < (int) strlen(option_string) + 1) {
                                return 0;
                        }
                        if (rlength > (int) strlen(option_string) + 1) {
                                return -EIO;
                        }
                        data = medusa_buffer_linearize(rbuffer, 0, rlength);
                        if (data == NULL) {
                                return -ENOMEM;
                        }
                        rc = strcmp(data, option_string);
                        if (rc != 0) {
                                return -EIO;
                        }
                }
                g_running = 0;
        }

        if (events & MEDUSA_TCPSOCKET_EVENT_DESTROY) {
        }

        return 0;
}

static int sender_medusa_udpsocket_onevent (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param)
{
        int rc;
        char buffer[1600];
        const char *option_string = context;

        (void) param;

        verbosef(1, "udpsocket events: 0x%08x, %s", events, medusa_udpsocket_event_string(events));

        if (events & MEDUSA_UDPSOCKET_EVENT_CONNECTED) {
                verbosef(0, "sending: %s", option_string);
                rc = send(medusa_udpsocket_get_fd(udpsocket), option_string, strlen(option_string) + 1, 0);
                if (rc != (int) (strlen(option_string) + 1)) {
                        fprintf(stderr, "can not send data to udpsocket\n");
                        goto bail;
                }
        } else if (events & MEDUSA_UDPSOCKET_EVENT_IN) {
                rc = recv(medusa_udpsocket_get_fd(udpsocket), buffer, sizeof(buffer), 0);
                if (rc != (int) (strlen(option_string) + 1)) {
                        fprintf(stderr, "can not recv data from udpsocket\n");
                        goto bail;
                }
                verbosef(0, "received: %s", buffer);
                if (memcmp(option_string, buffer, strlen(option_string) + 1) != 0) {
                        fprintf(stderr, "can not recv data from udpsocket\n");
                        return -EIO;
                }
                g_running = 0;
        }

        return 0;
bail:   return -1;
}

static int sigpipe_medusa_signal_onevent (struct medusa_signal *signal, unsigned int events, void *context, void *param)
{
        (void) signal;
        (void) events;
        (void) context;
        (void) param;
        return 0;
}

int main (int argc, char *argv[])
{
        int rc;
        int err;

        int c;
        int option_dport;
        int option_sport;
        const char *option_address;
        const char *option_protocol;
        const char *option_string;

        struct medusa_signal *medusa_signal;
        struct medusa_signal_init_options medusa_signal_init_options;

        struct medusa_tcpsocket *medusa_tcpsocket;
        struct medusa_tcpsocket_connect_options medusa_tcpsocket_connect_options;

        struct medusa_udpsocket *medusa_udpsocket;
        struct medusa_udpsocket_connect_options medusa_udpsocket_connect_options;

        struct medusa_buffer *medusa_tcpsocket_wbuffer;

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

        err = 0;
        medusa_monitor = NULL;

        option_dport    = OPTION_DPORT_DEFAULT;
        option_sport    = OPTION_SPORT_DEFAULT;
        option_protocol = OPTION_PROTOCOL_DEFAULT;
        option_address  = OPTION_ADDRESS_DEFAULT;
        option_string   = OPTION_STRING_DEFAULT;

        g_use_iovec     = OPTION_IOVEC_DEFAULT;
        g_use_ssl       = OPTION_SSL_DEFAULT;

        g_verbose       = OPTION_VERBOSE_DEFAULT;

        g_running = 1;

        while ((c = getopt_long(argc, argv, "hr:a:p:P:s:i:S:v:", longopts, NULL)) != -1) {
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
                        case OPTION_DPORT:
                                option_dport = atoi(optarg);
                                break;
                        case OPTION_SPORT:
                                option_sport = atoi(optarg);
                                break;
                        case OPTION_STRING:
                                option_string = optarg;
                                break;
                        case OPTION_IOVEC:
                                g_use_iovec = !!atoi(optarg);
                                break;
                        case OPTION_SSL:
                                g_use_ssl = !!atoi(optarg);
                                break;
                        case OPTION_VERBOSE:
                                g_verbose = atoi(optarg);
                                break;
                        default:
                                fprintf(stderr, "unknown option: %d\n", optopt);
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
        medusa_signal_init_options.number     = SIGPIPE;
        medusa_signal_init_options.onevent    = sigpipe_medusa_signal_onevent;
        medusa_signal_init_options.context    = NULL;
        medusa_signal_init_options.singleshot = 0;
        medusa_signal_init_options.enabled    = 1;
        medusa_signal_init_options.monitor    = medusa_monitor;
        medusa_signal = medusa_signal_create_with_options(&medusa_signal_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(medusa_signal)) {
                err = MEDUSA_PTR_ERR(medusa_signal);
                goto out;
        }

        verbosef(1, "connecting to %s://%s:%d (source port: %d)", option_protocol, option_address, option_dport, option_sport);

        if (strcasecmp(option_protocol, "t") == 0 || strcasecmp(option_protocol, "tcp") == 0) {
                rc = medusa_tcpsocket_connect_options_default(&medusa_tcpsocket_connect_options);
                if (rc < 0) {
                        err = rc;
                        goto out;
                }
                medusa_tcpsocket_connect_options.monitor     = medusa_monitor;
                medusa_tcpsocket_connect_options.onevent     = sender_medusa_tcpsocket_onevent;
                medusa_tcpsocket_connect_options.context     = (void *) option_string;
                medusa_tcpsocket_connect_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
                medusa_tcpsocket_connect_options.address     = option_address;
                medusa_tcpsocket_connect_options.port        = option_dport;
                medusa_tcpsocket_connect_options.sport       = option_sport;
                medusa_tcpsocket_connect_options.nonblocking = 1;
                medusa_tcpsocket_connect_options.buffered    = 1;
                medusa_tcpsocket_connect_options.enabled     = 1;
                medusa_tcpsocket = medusa_tcpsocket_connect_with_options(&medusa_tcpsocket_connect_options);
                if (MEDUSA_IS_ERR_OR_NULL(medusa_tcpsocket)) {
                        err = MEDUSA_PTR_ERR(medusa_tcpsocket);
                        goto out;
                }

                medusa_tcpsocket_wbuffer = medusa_tcpsocket_get_write_buffer(medusa_tcpsocket);
                if (MEDUSA_IS_ERR_OR_NULL(medusa_tcpsocket_wbuffer)) {
                        err = MEDUSA_PTR_ERR(medusa_tcpsocket_wbuffer);
                        goto out;
                }
                rc = medusa_buffer_append(medusa_tcpsocket_wbuffer, option_string, strlen(option_string) + 1);
                if (rc != (int) strlen(option_string) + 1) {
                        fprintf(stderr, "can not append to tcpsocket write buffer\n");
                        err = rc;
                        goto out;
                }
        } else if (strcasecmp(option_protocol, "u") == 0 || strcasecmp(option_protocol, "udp") == 0) {
                rc = medusa_udpsocket_connect_options_default(&medusa_udpsocket_connect_options);
                if (rc < 0) {
                        err = rc;
                        goto out;
                }
                medusa_udpsocket_connect_options.monitor     = medusa_monitor;
                medusa_udpsocket_connect_options.onevent     = sender_medusa_udpsocket_onevent;
                medusa_udpsocket_connect_options.context     = (void *) option_string;
                medusa_udpsocket_connect_options.protocol    = MEDUSA_TCPSOCKET_PROTOCOL_ANY;
                medusa_udpsocket_connect_options.address     = option_address;
                medusa_udpsocket_connect_options.port        = option_dport;
                medusa_udpsocket_connect_options.nonblocking = 1;
                medusa_udpsocket_connect_options.enabled     = 1;
                medusa_udpsocket = medusa_udpsocket_connect_with_options(&medusa_udpsocket_connect_options);
                if (MEDUSA_IS_ERR_OR_NULL(medusa_udpsocket)) {
                        err = MEDUSA_PTR_ERR(medusa_udpsocket);
                        goto out;
                }
        } else {
                fprintf(stderr, "option_protocol: %s is invalid\n", option_protocol);
                err = -EINVAL;
                goto out;
        }

        while (g_running == 1) {
                rc = medusa_monitor_run_once(medusa_monitor);
                if (rc < 0) {
                        err = rc;
                        goto out;
                }
        }

out:    if (!MEDUSA_IS_ERR_OR_NULL(medusa_monitor)) {
                medusa_monitor_destroy(medusa_monitor);
        }
        return err;
}
