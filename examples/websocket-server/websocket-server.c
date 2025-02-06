
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <getopt.h>
#include <signal.h>

#if defined(__WINDOWS__)
#include <winsock2.h>
#endif

#include <medusa/error.h>
#include <medusa/websocketserver.h>
#include <medusa/monitor.h>

#define OPTIONS_DEFAULT_ADDRESS                 "127.0.0.1"
#define OPTIONS_DEFAULT_PORT                    12345
#define OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT     -1

#define OPTION_HELP                     'h'
#define OPTION_ADDRESS                  'a'
#define OPTION_PORT                     'p'
#define OPTION_CLIENT_READ_TIMEOUT      'r'

static const char *g_option_address     = OPTIONS_DEFAULT_ADDRESS;
static int g_option_port                = OPTIONS_DEFAULT_PORT;
static int g_option_client_read_timeout = OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT;

static int g_running = 0;

static struct option longopts[] = {
        { "help",               no_argument,            NULL,        OPTION_HELP                },
        { "address",            required_argument,      NULL,        OPTION_ADDRESS             },
        { "port",               required_argument,      NULL,        OPTION_PORT                },
        { "client-read-timeout",required_argument,      NULL,        OPTION_CLIENT_READ_TIMEOUT },
        { NULL,                 0,                      NULL,        0                          },
};

static void usage (const char *pname)
{
        fprintf(stdout, "medusa http server\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "usage:\n");
        fprintf(stdout, "  %s [options]\n", pname);
        fprintf(stdout, "\n");
        fprintf(stdout, "options:\n");
        fprintf(stdout, "  -a, --address            : address to run on (default: %s)\n", OPTIONS_DEFAULT_ADDRESS);
        fprintf(stdout, "  -p, --port               : port to run on (default: %d)\n", OPTIONS_DEFAULT_PORT);
        fprintf(stdout, "  -r, --client-read-timeout: client read timeout in milliseconds (default: %d)\n", OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT);
        fprintf(stdout, "\n");
        fprintf(stdout, "example:\n");
        fprintf(stdout, "  %s -a 127.0.0.1 -p 12345\n", pname);
}

static int websocketserver_client_onevent (struct medusa_websocketserver_client *websocketserver_client, unsigned int events, void *context, void *param)
{
        (void) context;
        (void) param;

        fprintf(stderr, "websocketserver_client state: %d, %s events: 0x%08x, %s\n", medusa_websocketserver_client_get_state(websocketserver_client), medusa_websocketserver_client_state_string(medusa_websocketserver_client_get_state(websocketserver_client)), events, medusa_websocketserver_client_event_string(events));

        if (events & MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_REQUEST_HEADER) {
                struct medusa_websocketserver_client_event_request_header *request_header = (struct medusa_websocketserver_client_event_request_header *) param;
                fprintf(stderr, "header: '%s': '%s'\n", request_header->field, request_header->value);
        }
        if (events & MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_MESSAGE) {
                int rc;
                struct medusa_websocketserver_client_event_message *medusa_websocketserver_client_event_message = (struct medusa_websocketserver_client_event_message *) param;
                fprintf(stderr, "  final  : %d\n", medusa_websocketserver_client_event_message->final);
                fprintf(stderr, "  type   : %d, %s\n", medusa_websocketserver_client_event_message->type, medusa_websocketserver_client_frame_type_string(medusa_websocketserver_client_event_message->type));
                fprintf(stderr, "  length : %d\n", medusa_websocketserver_client_event_message->length);
                fprintf(stderr, "  payload: %p\n", medusa_websocketserver_client_event_message->payload);
                if (medusa_websocketserver_client_event_message->type == MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_TEXT) {
                        fprintf(stderr, "    '%.*s'\n", medusa_websocketserver_client_event_message->length, (const char *) medusa_websocketserver_client_event_message->payload);
                        rc = medusa_websocketserver_client_write(websocketserver_client, 1, MEDUSA_WEBSOCKETSERVER_CLIENT_FRAME_TYPE_TEXT, medusa_websocketserver_client_event_message->payload, medusa_websocketserver_client_event_message->length);
                        if (rc < 0) {
                                fprintf(stderr, "can not send message\n");
                                return -1;
                        }
                }
        }

        return 0;
}

static int websocketserver_onevent (struct medusa_websocketserver *websocketserver, unsigned int events, void *context, void *param)
{
        int rc;
        struct medusa_websocketserver_accept_options websocketserver_accept_options;

        struct medusa_websocketserver_client *websocketserver_client;

        (void) websocketserver;
        (void) events;
        (void) context;
        (void) param;

        fprintf(stderr, "websocketserver state: %d, %s events: 0x%08x, %s\n", medusa_websocketserver_get_state(websocketserver), medusa_websocketserver_state_string(medusa_websocketserver_get_state(websocketserver)), events, medusa_websocketserver_event_string(events));

        if (events & MEDUSA_WEBSOCKETSERVER_EVENT_ERROR) {
                medusa_monitor_break(medusa_websocketserver_get_monitor(websocketserver));
        } else if (events & MEDUSA_WEBSOCKETSERVER_EVENT_CONNECTION) {
                rc = medusa_websocketserver_accept_options_default(&websocketserver_accept_options);
                if (rc != 0) {
                        fprintf(stderr, "can not get default accept options\n");
                        goto bail;
                }
                websocketserver_accept_options.onevent = websocketserver_client_onevent;
                websocketserver_accept_options.context = NULL;
                websocketserver_client = medusa_websocketserver_accept_with_options(websocketserver, &websocketserver_accept_options);
                if (MEDUSA_IS_ERR_OR_NULL(websocketserver_client)) {
                        fprintf(stderr, "can not accept websocketserver client\n");
                        goto bail;
                }
                rc = medusa_websocketserver_client_set_enabled(websocketserver_client, 1);
                if (rc != 0) {
                        fprintf(stderr, "can not enable websocketserver client\n");
                        goto bail;
                }
        }
        return 0;
bail:   return -1;
}

static void sigint_handler (int sig)
{
        (void) sig;
        g_running = 0;
}

int main (int argc, char *argv[])
{
        int c;
        int _argc;
        char **_argv;

        int rc;
        struct medusa_monitor *monitor;

        struct medusa_websocketserver_init_options websocketserver_init_options;
        struct medusa_websocketserver *websocketserver;

#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

        g_running = 1;
        signal(SIGINT, sigint_handler);

        monitor = NULL;

        g_option_address             = OPTIONS_DEFAULT_ADDRESS;
        g_option_port                = OPTIONS_DEFAULT_PORT;
        g_option_client_read_timeout = OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT;

        _argv = malloc(sizeof(char *) * (argc + 1));

        optind = 0;
        for (_argc = 0; _argc < argc; _argc++) {
                _argv[_argc] = argv[_argc];
        }
        while ((c = getopt_long(_argc, _argv, "ha:p:r:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HELP:
                                usage(argv[0]);
                                goto out;
                        case OPTION_ADDRESS:
                                g_option_address = optarg;
                                break;
                        case OPTION_PORT:
                                g_option_port = atoi(optarg);
                                break;
                        case OPTION_CLIENT_READ_TIMEOUT:
                                g_option_client_read_timeout = atoi(optarg);
                                break;
                        default:
                                fprintf(stderr, "invalid option: %s\n", argv[optind - 1]);
                                goto bail;
                }
        }

        monitor = medusa_monitor_create_with_options(NULL);
        if (monitor == NULL) {
                fprintf(stderr, "can not create monitor\n");
                goto bail;
        }

        medusa_websocketserver_init_options_default(&websocketserver_init_options);
        websocketserver_init_options.monitor  = monitor;
        websocketserver_init_options.protocol = MEDUSA_WEBSOCKETSERVER_PROTOCOL_ANY;
        websocketserver_init_options.address  = g_option_address;
        websocketserver_init_options.port     = g_option_port;
        websocketserver_init_options.servername = NULL;
        websocketserver_init_options.enabled  = 0;
        websocketserver_init_options.started  = 0;
        websocketserver_init_options.onevent  = websocketserver_onevent;
        websocketserver_init_options.context  = NULL;

        websocketserver = medusa_websocketserver_create_with_options(&websocketserver_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(websocketserver)) {
                fprintf(stderr, "can not create websocketserver errno: %d, %s\n", MEDUSA_PTR_ERR(websocketserver), strerror(MEDUSA_PTR_ERR(websocketserver)));
                goto bail;
        }
        rc = medusa_websocketserver_set_enabled(websocketserver, 1);
        if (rc != 0) {
                fprintf(stderr, "can not enable websocketserver\n");
                goto bail;
        }
        rc = medusa_websocketserver_set_started(websocketserver, 1);
        if (rc != 0) {
                fprintf(stderr, "can not start websocketserver\n");
                goto bail;
        }

        while (g_running) {
                rc = medusa_monitor_run_timeout(monitor, 1.0);
                if (rc < 0) {
                        fprintf(stderr, "monitor failed\n");
                        goto bail;
                }
                if (rc == 0) {
                        break;
                }
        }

        medusa_monitor_destroy(monitor);
out:    free(_argv);
        return 0;

bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        free(_argv);
        return -1;
}
