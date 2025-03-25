
#define _GNU_SOURCE

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
#include <medusa/httpserver.h>
#include <medusa/monitor.h>

#define OPTIONS_DEFAULT_ADDRESS                 "127.0.0.1"
#define OPTIONS_DEFAULT_PORT                    12345
#define OPTIONS_DEFAULT_FOLDER                  "./"
#define OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT     -1

#define OPTION_HELP                     'h'
#define OPTION_ADDRESS                  'a'
#define OPTION_PORT                     'p'
#define OPTIONS_FOLDER                  'f'
#define OPTION_CLIENT_READ_TIMEOUT      'r'

static const char *g_option_address     = OPTIONS_DEFAULT_ADDRESS;
static int g_option_port                = OPTIONS_DEFAULT_PORT;
static const char *g_option_folder      = OPTIONS_DEFAULT_FOLDER;
static int g_option_client_read_timeout = OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT;

static int g_running = 0;

static struct option longopts[] = {
        { "help",               no_argument,            NULL,        OPTION_HELP                },
        { "address",            required_argument,      NULL,        OPTION_ADDRESS             },
        { "port",               required_argument,      NULL,        OPTION_PORT                },
        { "folder"             ,required_argument,      NULL,        OPTIONS_FOLDER             },
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
        fprintf(stdout, "  -f, --folder             : folder to serve (default: %s)\n", OPTIONS_DEFAULT_FOLDER);
        fprintf(stdout, "  -r, --client-read-timeout: client read timeout in milliseconds (default: %d)\n", OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT);
        fprintf(stdout, "\n");
        fprintf(stdout, "example:\n");
        fprintf(stdout, "  %s -a 127.0.0.1 -p 12345\n", pname);
}

static int httpserver_client_onevent (struct medusa_httpserver_client *httpserver_client, unsigned int events, void *context, void *param)
{
        int rc;

        (void) context;
        (void) param;

        fprintf(stderr, "httpserver_client state: %d, %s events: 0x%08x, %s\n", medusa_httpserver_client_get_state(httpserver_client), medusa_httpserver_client_state_string(medusa_httpserver_client_get_state(httpserver_client)), events, medusa_httpserver_client_event_string(events));
        if (events & MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVED) {
                struct medusa_httpserver_client_event_request_received *httpserver_client_event_request_received = (struct medusa_httpserver_client_event_request_received *) param;

                const struct medusa_httpserver_client_request *httpserver_client_request;
                const struct medusa_httpserver_client_request_option *httpserver_client_request_option;
                const struct medusa_httpserver_client_request_options *httpserver_client_request_options;
                const struct medusa_httpserver_client_request_header *httpserver_client_request_header;
                const struct medusa_httpserver_client_request_headers *httpserver_client_request_headers;
                const struct medusa_httpserver_client_request_body *httpserver_client_request_body;

                httpserver_client_request = httpserver_client_event_request_received->request;
                if (httpserver_client_request != medusa_httprequest_client_get_request(httpserver_client)) {
                        fprintf(stderr, "httpserver client request logic error\n");
                        goto bail;
                }

                fprintf(stderr, "method : %s\n", medusa_httpserver_client_request_get_method(httpserver_client_request));
                fprintf(stderr, "url    : %s\n", medusa_httpserver_client_request_get_url(httpserver_client_request));

                fprintf(stderr, "path   : %s\n", medusa_httpserver_client_request_get_path(httpserver_client_request));
                httpserver_client_request_options = medusa_httpserver_client_request_get_options(httpserver_client_request);
                if (MEDUSA_IS_ERR_OR_NULL(httpserver_client_request_options)) {
                        fprintf(stderr, "hettprequest reply options is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "options\n");
                fprintf(stderr, "  count: %d\n", (int) medusa_httpserver_client_request_options_get_count(httpserver_client_request_options));
                for (httpserver_client_request_option = medusa_httpserver_client_request_options_get_first(httpserver_client_request_options);
                     httpserver_client_request_option;
                     httpserver_client_request_option = medusa_httpserver_client_request_option_get_next(httpserver_client_request_option)) {
                        fprintf(stderr, "  %s = %s\n",
                                medusa_httpserver_client_request_option_get_key(httpserver_client_request_option),
                                medusa_httpserver_client_request_option_get_value(httpserver_client_request_option));
                }

                httpserver_client_request_headers = medusa_httpserver_client_request_get_headers(httpserver_client_request);
                if (MEDUSA_IS_ERR_OR_NULL(httpserver_client_request_headers)) {
                        fprintf(stderr, "hettprequest reply headers is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "headers\n");
                fprintf(stderr, "  count: %lld\n", (long long int) medusa_httpserver_client_request_headers_get_count(httpserver_client_request_headers));
                for (httpserver_client_request_header = medusa_httpserver_client_request_headers_get_first(httpserver_client_request_headers);
                     httpserver_client_request_header;
                     httpserver_client_request_header = medusa_httpserver_client_request_header_get_next(httpserver_client_request_header)) {
                        fprintf(stderr, "  %s = %s\n",
                                medusa_httpserver_client_request_header_get_key(httpserver_client_request_header),
                                medusa_httpserver_client_request_header_get_value(httpserver_client_request_header));
                }

                if (strcasecmp(medusa_httpserver_client_request_get_method(httpserver_client_request), "GET") == 0) {
                        int rc;
                        FILE *fp = NULL;
                        char *fn = NULL;
                        rc = asprintf(&fn, "%s/%s", g_option_folder, medusa_httpserver_client_request_get_path(httpserver_client_request));
                        if (rc < 0) {
                                fprintf(stderr, "can not build file name\n");
                                goto bail;
                        }
                        fp = fopen(fn, "rb");
                        if (fp == NULL) {
                                fprintf(stderr, "can not open file: %s\n", fn);

                                rc  = medusa_httpserver_client_reply_send_start(httpserver_client);
                                rc |= medusa_httpserver_client_reply_send_status(httpserver_client, "1.1", 404, "Not Found");
                                rc |= medusa_httpserver_client_reply_send_header(httpserver_client, "key", "value");
                                rc |= medusa_httpserver_client_reply_send_headerf(httpserver_client, "Content-Length", "%d", 0);
                                rc |= medusa_httpserver_client_reply_send_header(httpserver_client, NULL, NULL);
                                rc |= medusa_httpserver_client_reply_send_finish(httpserver_client);
                                if (rc != 0) {
                                        fprintf(stderr, "can not send httpserver client reply\n");
                                        goto bail;
                                }
                        } else {
                                rc  = medusa_httpserver_client_reply_send_start(httpserver_client);
                                rc |= medusa_httpserver_client_reply_send_status(httpserver_client, "1.0", 200, "200");
                                rc |= medusa_httpserver_client_reply_send_header(httpserver_client, NULL, NULL);

                                while (1) {
                                        int len;
                                        char buf[1024];
                                        len = fread(buf, 1, sizeof(buf), fp);
                                        if (len < 0) {
                                                fprintf(stderr, "can not read file: %s\n", fn);
                                        } else if (len == 0) {
                                                break;
                                        } else {
                                                rc |= medusa_httpserver_client_reply_send_body(httpserver_client, buf, len);
                                        }
                                }
                                fclose(fp);

                                rc |= medusa_httpserver_client_reply_send_finish(httpserver_client);
                                if (rc != 0) {
                                        fprintf(stderr, "can not send httpserver client reply\n");
                                        free(fn);
                                        goto bail;
                                }
                        }
                        free(fn);
                } else if (strcasecmp(medusa_httpserver_client_request_get_method(httpserver_client_request), "POST") == 0) {
                        httpserver_client_request_body = medusa_httpserver_client_request_get_body(httpserver_client_request);
                        if (MEDUSA_IS_ERR_OR_NULL(httpserver_client_request_body)) {
                                fprintf(stderr, "hettprequest reply body is invalid\n");
                                goto bail;
                        }
                        fprintf(stderr, "body\n");
                        fprintf(stderr, "  length: %lld\n", (long long int) medusa_httpserver_client_request_body_get_length(httpserver_client_request_body));
                        fprintf(stderr, "  value : %.*s\n",
                                (int) medusa_httpserver_client_request_body_get_length(httpserver_client_request_body),
                                (char *) medusa_httpserver_client_request_body_get_value(httpserver_client_request_body));

                        rc  = medusa_httpserver_client_reply_send_start(httpserver_client);
                        rc |= medusa_httpserver_client_reply_send_status(httpserver_client, "1.1", 200, "OK");
                        rc |= medusa_httpserver_client_reply_send_header(httpserver_client, "key", "value");
                        rc |= medusa_httpserver_client_reply_send_headerf(httpserver_client, "Content-Length", "%d", (int) strlen("OK"));
                        rc |= medusa_httpserver_client_reply_send_header(httpserver_client, NULL, NULL);
                        rc |= medusa_httpserver_client_reply_send_bodyf(httpserver_client, "OK");
                        rc |= medusa_httpserver_client_reply_send_finish(httpserver_client);
                        if (rc != 0) {
                                fprintf(stderr, "can not send httpserver client reply\n");
                                goto bail;
                        }
                } else {
                        rc  = medusa_httpserver_client_reply_send_start(httpserver_client);
                        rc |= medusa_httpserver_client_reply_send_status(httpserver_client, "1.1", 500, "Internal Server Error");
                        rc |= medusa_httpserver_client_reply_send_headerf(httpserver_client, "Content-Length", "%d", (int) strlen("Internal Server Error"));
                        rc |= medusa_httpserver_client_reply_send_header(httpserver_client, NULL, NULL);
                        rc |= medusa_httpserver_client_reply_send_bodyf(httpserver_client, "Internal Server Error");
                        rc |= medusa_httpserver_client_reply_send_finish(httpserver_client);
                        if (rc != 0) {
                                fprintf(stderr, "can not send httpserver client reply\n");
                                goto bail;
                        }
                }
        } else if (events & MEDUSA_HTTPSERVER_CLIENT_EVENT_REQUEST_RECEIVE_TIMEOUT) {
                medusa_httpserver_client_destroy(httpserver_client);
        } else if (events & MEDUSA_HTTPSERVER_CLIENT_EVENT_BUFFERED_WRITE_FINISHED) {
                medusa_httpserver_client_destroy(httpserver_client);
        }
        return 0;
bail:   return -1;
}

static int httpserver_onevent (struct medusa_httpserver *httpserver, unsigned int events, void *context, void *param)
{
        int rc;
        struct medusa_httpserver_accept_options httpserver_accept_options;

        struct medusa_httpserver_client *httpserver_client;
        (void) httpserver;
        (void) events;
        (void) context;
        (void) param;
        fprintf(stderr, "httpserver state: %d, %s events: 0x%08x, %s\n", medusa_httpserver_get_state(httpserver), medusa_httpserver_state_string(medusa_httpserver_get_state(httpserver)), events, medusa_httpserver_event_string(events));
        if (events & MEDUSA_HTTPSERVER_EVENT_ERROR) {
                medusa_monitor_break(medusa_httpserver_get_monitor(httpserver));
        } else if (events & MEDUSA_HTTPSERVER_EVENT_CONNECTION) {
                rc = medusa_httpserver_accept_options_default(&httpserver_accept_options);
                if (rc != 0) {
                        fprintf(stderr, "can not get default accept options\n");
                        goto bail;
                }
                httpserver_accept_options.onevent = httpserver_client_onevent;
                httpserver_accept_options.context = NULL;
                httpserver_client = medusa_httpserver_accept_with_options(httpserver, &httpserver_accept_options);
                if (MEDUSA_IS_ERR_OR_NULL(httpserver_client)) {
                        fprintf(stderr, "can not accept httpserver client\n");
                        goto bail;
                }
                rc = medusa_httpserver_client_set_read_timeout(httpserver_client, g_option_client_read_timeout / 1000.00);
                if (rc != 0) {
                        fprintf(stderr, "can not server read timeout for httpserver client\n");
                        goto bail;
                }
                rc = medusa_httpserver_client_set_enabled(httpserver_client, 1);
                if (rc != 0) {
                        fprintf(stderr, "can not enable httpserver client\n");
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

        struct medusa_httpserver_init_options httpserver_init_options;
        struct medusa_httpserver *httpserver;

#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

        g_running = 1;
        signal(SIGINT, sigint_handler);

        monitor = NULL;

        g_option_address             = OPTIONS_DEFAULT_ADDRESS;
        g_option_port                = OPTIONS_DEFAULT_PORT;
        g_option_folder              = OPTIONS_DEFAULT_FOLDER;
        g_option_client_read_timeout = OPTIONS_DEFAULT_CLIENT_READ_TIMEOUT;

        _argv = malloc(sizeof(char *) * (argc + 1));

        optind = 0;
        for (_argc = 0; _argc < argc; _argc++) {
                _argv[_argc] = argv[_argc];
        }
        while ((c = getopt_long(_argc, _argv, "ha:p:r:f:", longopts, NULL)) != -1) {
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
                        case OPTIONS_FOLDER:
                                g_option_folder = optarg;
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

        medusa_httpserver_init_options_default(&httpserver_init_options);
        httpserver_init_options.address  = g_option_address;
        httpserver_init_options.port     = g_option_port;
        httpserver_init_options.enabled  = 0;
        httpserver_init_options.started  = 0;
        httpserver_init_options.monitor  = monitor;
        httpserver_init_options.onevent  = httpserver_onevent;
        httpserver_init_options.context  = NULL;

        httpserver = medusa_httpserver_create_with_options(&httpserver_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(httpserver)) {
                fprintf(stderr, "can not create httpserver errno: %d, %s\n", MEDUSA_PTR_ERR(httpserver), strerror(MEDUSA_PTR_ERR(httpserver)));
                goto bail;
        }
        rc = medusa_httpserver_set_enabled(httpserver, 1);
        if (rc != 0) {
                fprintf(stderr, "can not enable httpserver\n");
                goto bail;
        }
        rc = medusa_httpserver_set_started(httpserver, 1);
        if (rc != 0) {
                fprintf(stderr, "can not start httpserver\n");
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
