
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <getopt.h>
#include <signal.h>

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <medusa/error.h>
#include <medusa/url.h>
#include <medusa/websocketclient.h>
#include <medusa/monitor.h>

#define OPTIONS_DEFAULT_URL                     "ws://127.0.0.1:80/path"
#define OPTIONS_DEFAULT_MESSAGE                 "hello"
#define OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT     -1

#define OPTION_HELP                     'h'
#define OPTION_URL                      'u'
#define OPTION_MESSAGE                  'm'
#define OPTION_READ_TIMEOUT             'r'

static const char *g_option_url         = OPTIONS_DEFAULT_URL;
static const char *g_option_message     = OPTIONS_DEFAULT_MESSAGE;

static int g_running = 0;

static struct option longopts[] = {
        { "help",               no_argument,            NULL,        OPTION_HELP                },
        { "url",                required_argument,      NULL,        OPTION_URL                 },
        { "message",            required_argument,      NULL,        OPTION_MESSAGE             },
        { NULL,                 0,                      NULL,        0                          },
};

static void usage (const char *pname)
{
        fprintf(stdout, "medusa websocket client\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "usage:\n");
        fprintf(stdout, "  %s [options]\n", pname);
        fprintf(stdout, "\n");
        fprintf(stdout, "options:\n");
        fprintf(stdout, "  -u, --url     : url to connect (default: %s)\n", OPTIONS_DEFAULT_URL);
        fprintf(stdout, "  -m, --message : message to send (default: %s)\n", OPTIONS_DEFAULT_MESSAGE);
        fprintf(stdout, "\n");
        fprintf(stdout, "example:\n");
        fprintf(stdout, "  %s -u ws://127.0.0.1:80/path\n", pname);
}

static int websocketclient_onevent (struct medusa_websocketclient *websocketclient, unsigned int events, void *context, void *param)
{
        (void) context;
        (void) param;

        fprintf(stderr, "websocketclient state: %d, %s events: 0x%08x, %s\n", medusa_websocketclient_get_state(websocketclient), medusa_websocketclient_state_string(medusa_websocketclient_get_state(websocketclient)), events, medusa_websocketclient_event_string(events));

        if (events & MEDUSA_WEBSOCKETCLIENT_EVENT_RESPONSE_HEADER) {
                struct medusa_websocketclient_event_response_header *response_header = (struct medusa_websocketclient_event_response_header *) param;
                fprintf(stderr, "header: '%s': '%s'\n", response_header->field, response_header->value);
        }
        if (events & MEDUSA_WEBSOCKETCLIENT_EVENT_MESSAGE) {
                struct medusa_websocketclient_event_message *medusa_websocketclient_event_message = (struct medusa_websocketclient_event_message *) param;
                fprintf(stderr, "  final  : %d\n", medusa_websocketclient_event_message->final);
                fprintf(stderr, "  type   : %d, %s\n", medusa_websocketclient_event_message->type, medusa_websocketclient_frame_type_string(medusa_websocketclient_event_message->type));
                fprintf(stderr, "  length : %d\n", medusa_websocketclient_event_message->length);
                fprintf(stderr, "  payload: %p\n", medusa_websocketclient_event_message->payload);
                if (medusa_websocketclient_event_message->type == MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT) {
                        fprintf(stderr, "    '%.*s'\n", medusa_websocketclient_event_message->length, (const char *) medusa_websocketclient_event_message->payload);
                }
        }
        if (events & MEDUSA_WEBSOCKETCLIENT_EVENT_CONNECTED) {
                int rc;
                rc = medusa_websocketclient_write(websocketclient, 1, MEDUSA_WEBSOCKETCLIENT_FRAME_TYPE_TEXT, g_option_message, strlen(g_option_message) + 1);
                if (rc < 0) {
                        fprintf(stderr, "can not send message\n");
                        return -1;
                }
        }

        return 0;
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

        struct medusa_websocketclient_connect_options websocketclient_connect_options;
        struct medusa_websocketclient *websocketclient;
        struct medusa_url *url;

#if defined(_WIN32)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

        g_running = 1;
        signal(SIGINT, sigint_handler);

        monitor = NULL;

        g_option_url     = OPTIONS_DEFAULT_URL;
        g_option_message = OPTIONS_DEFAULT_MESSAGE;

        _argv = malloc(sizeof(char *) * (argc + 1));

        optind = 0;
        for (_argc = 0; _argc < argc; _argc++) {
                _argv[_argc] = argv[_argc];
        }
        while ((c = getopt_long(_argc, _argv, "hu:m:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HELP:
                                usage(argv[0]);
                                goto out;
                        case OPTION_URL:
                                g_option_url = optarg;
                                break;
                        case OPTION_MESSAGE:
                                g_option_message = optarg;
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

        url = medusa_url_parse(g_option_url);
        if (url == NULL) {
                fprintf(stderr, "url is invalid\n");
                goto bail;
        }

        medusa_websocketclient_connect_options_default(&websocketclient_connect_options);
        websocketclient_connect_options.monitor                 = monitor;
        websocketclient_connect_options.protocol                = MEDUSA_WEBSOCKETCLIENT_PROTOCOL_ANY;
        websocketclient_connect_options.address                 = medusa_url_get_host(url);
        websocketclient_connect_options.port                    = medusa_url_get_port(url);
        websocketclient_connect_options.server_path             = medusa_url_get_path(url);
        websocketclient_connect_options.server_protocol         = "generic";
        websocketclient_connect_options.ssl_certificate         = NULL;
        websocketclient_connect_options.ssl_privatekey          = NULL;
        websocketclient_connect_options.ssl_ca_certificate      = NULL;
        websocketclient_connect_options.ssl_verify              = 0;
        websocketclient_connect_options.ssl                     = (medusa_url_get_scheme(url) != NULL) &&
                                                                  (strcasecmp(medusa_url_get_scheme(url), "wss") == 0 ||
                                                                   strcasecmp(medusa_url_get_scheme(url), "https") == 0);
        websocketclient_connect_options.enabled                 = 1;
        websocketclient_connect_options.onevent                 = websocketclient_onevent;
        websocketclient_connect_options.context                 = NULL;

        websocketclient = medusa_websocketclient_connect_with_options(&websocketclient_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(websocketclient)) {
                fprintf(stderr, "can not create websocketclient errno: %d, %s\n", MEDUSA_PTR_ERR(websocketclient), strerror(MEDUSA_PTR_ERR(websocketclient)));
                goto bail;
        }
        rc = medusa_websocketclient_set_enabled(websocketclient, 1);
        if (rc != 0) {
                fprintf(stderr, "can not enable websocketclient\n");
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
