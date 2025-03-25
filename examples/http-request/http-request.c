
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <getopt.h>

#if defined(__WINDOWS__)
#include <winsock2.h>
#endif

#include <medusa/error.h>
#include <medusa/httprequest.h>
#include <medusa/monitor.h>

#define OPTIONS_DEFAULT_URL             "http://127.0.0.1"
#define OPTIONS_DEFAULT_METHOD          "get"
#define OPTIONS_DEFAULT_DATA            NULL
#define OPTIONS_DEFAULT_CONNECT_TIMEOUT 5.0
#define OPTIONS_DEFAULT_READ_TIMEOUT    5.0

#define OPTION_HELP                     'h'
#define OPTION_URL                      'u'
#define OPTION_METHOD                   'm'
#define OPTION_HEADER                   'e'
#define OPTION_DATA                     'd'
#define OPTION_CONNECT_TIMEOUT          'c'
#define OPTION_READ_TIMEOUT             'r'

static struct option longopts[] = {
        { "help",               no_argument,            NULL,        OPTION_HELP                },
        { "url",                required_argument,      NULL,        OPTION_URL                 },
        { "method",             required_argument,      NULL,        OPTION_METHOD              },
        { "header",             required_argument,      NULL,        OPTION_HEADER              },
        { "data",               required_argument,      NULL,        OPTION_DATA                },
        { "connect-timeout",    required_argument,      NULL,        OPTION_CONNECT_TIMEOUT     },
        { "read-timeout",       required_argument,      NULL,        OPTION_READ_TIMEOUT        },
        { NULL,                 0,                      NULL,        0                          },
};

static void usage (const char *pname)
{
        fprintf(stdout, "medusa http request tool\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "usage:\n");
        fprintf(stdout, "  %s [options]\n", pname);
        fprintf(stdout, "\n");
        fprintf(stdout, "options:\n");
        fprintf(stdout, "  -u, --url   : request url (default: %s)\n", OPTIONS_DEFAULT_URL);
        fprintf(stdout, "  -m, --method: request method (default: %s)\n", OPTIONS_DEFAULT_METHOD);
        fprintf(stdout, "  -e, --header: add header\n");
        fprintf(stdout, "  -d, --data  : request data (default: %s)\n", (OPTIONS_DEFAULT_DATA) ? OPTIONS_DEFAULT_DATA : "(null)");
        fprintf(stdout, "  -c, --connect-timeout: connect timeout (default: %.2f)\n", OPTIONS_DEFAULT_CONNECT_TIMEOUT);
        fprintf(stdout, "  -r, --read-timeout   : read timeout (default: %.2f)\n", OPTIONS_DEFAULT_READ_TIMEOUT);
        fprintf(stdout, "  -h, --help  : this text\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "example:\n");
        fprintf(stdout, "  %s -u http://127.0.0.1/ -m get -h 'a:b' -h 'c:d'\n", pname);
        fprintf(stdout, "  %s -u http://127.0.0.1/ -m head -h 'a:b' -h 'c:d'\n", pname);
        fprintf(stdout, "  %s -u http://127.0.0.1/ -m post -h 'a:b' -h 'c:d' -d 'data'\n", pname);
}

static int httprequest_onevent (struct medusa_httprequest *httprequest, unsigned int events, void *context, void *param)
{
        (void) httprequest;
        (void) events;
        (void) context;
        (void) param;
        fprintf(stderr, "httprequest state: %d, %s events: 0x%08x, %s\n", medusa_httprequest_get_state(httprequest), medusa_httprequest_state_string(medusa_httprequest_get_state(httprequest)), events, medusa_httprequest_event_string(events));
        if (events & MEDUSA_HTTPREQUEST_EVENT_REQUESTED) {
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_RECEIVED) {
                const struct medusa_httprequest_reply *httprequest_reply;
                const struct medusa_httprequest_reply_status *httprequest_reply_status;
                const struct medusa_httprequest_reply_header *httprequest_reply_header;
                const struct medusa_httprequest_reply_headers *httprequest_reply_headers;
                const struct medusa_httprequest_reply_body *httprequest_reply_body;

                httprequest_reply = medusa_httprequest_get_reply(httprequest);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply)) {
                        fprintf(stderr, "hettprequest reply is invalid\n");
                        goto bail;
                }

                httprequest_reply_status = medusa_httprequest_reply_get_status(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_status)) {
                        fprintf(stderr, "hettprequest reply status is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "status:\n");
                fprintf(stderr, "  code : %lld\n", (long long int) medusa_httprequest_reply_status_get_code(httprequest_reply_status));
                fprintf(stderr, "  value: %s\n", medusa_httprequest_reply_status_get_value(httprequest_reply_status));

                httprequest_reply_headers = medusa_httprequest_reply_get_headers(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_headers)) {
                        fprintf(stderr, "hettprequest reply headers is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "headers:\n");
                fprintf(stderr, "  count: %lld\n", (long long int) medusa_httprequest_reply_headers_get_count(httprequest_reply_headers));
                for (httprequest_reply_header = medusa_httprequest_reply_headers_get_first(httprequest_reply_headers);
                     httprequest_reply_header;
                     httprequest_reply_header = medusa_httprequest_reply_header_get_next(httprequest_reply_header)) {
                        fprintf(stderr, "  %s = %s\n",
                                medusa_httprequest_reply_header_get_key(httprequest_reply_header),
                                medusa_httprequest_reply_header_get_value(httprequest_reply_header));
                }

                httprequest_reply_body = medusa_httprequest_reply_get_body(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_body)) {
                        fprintf(stderr, "hettprequest reply body is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "body\n");
                fprintf(stderr, "  length: %lld\n", (long long int) medusa_httprequest_reply_body_get_length(httprequest_reply_body));
                fprintf(stderr, "  value : %.*s\n",
                        (int) medusa_httprequest_reply_body_get_length(httprequest_reply_body),
                        (char *) medusa_httprequest_reply_body_get_value(httprequest_reply_body));

                medusa_monitor_break(medusa_httprequest_get_monitor(httprequest));
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_RECEIVE_TIMEOUT) {
                medusa_httprequest_destroy(httprequest);
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_DISCONNECTED) {
                medusa_monitor_break(medusa_httprequest_get_monitor(httprequest));
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_ERROR) {
                medusa_monitor_break(medusa_httprequest_get_monitor(httprequest));
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_DESTROY) {
                medusa_monitor_break(medusa_httprequest_get_monitor(httprequest));
        }
        return 0;
bail:   return -1;
}

int main (int argc, char *argv[])
{
        int c;
        int _argc;
        char **_argv;

        const char *option_url;
        const char *option_method;
        const char *option_data;
        double option_connect_timeout;
        double option_read_timeout;

        int rc;
        struct medusa_monitor *monitor;

        struct medusa_httprequest_init_options httprequest_init_options;
        struct medusa_httprequest *httprequest;

#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

        monitor = NULL;

        option_url             = OPTIONS_DEFAULT_URL;
        option_method          = OPTIONS_DEFAULT_METHOD;
        option_data            = NULL;
        option_connect_timeout = OPTIONS_DEFAULT_CONNECT_TIMEOUT;
        option_read_timeout    = OPTIONS_DEFAULT_READ_TIMEOUT;

        _argv = malloc(sizeof(char *) * (argc + 1));

        optind = 0;
        for (_argc = 0; _argc < argc; _argc++) {
                _argv[_argc] = argv[_argc];
        }
        while ((c = getopt_long(_argc, _argv, "hu:m:e:d:c:r:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HELP:
                                usage(argv[0]);
                                goto out;
                        case OPTION_URL:
                                option_url = optarg;
                                break;
                        case OPTION_METHOD:
                                option_method = optarg;
                                break;
                        case OPTION_HEADER:
                                break;
                        case OPTION_DATA:
                                option_data = optarg;
                                break;
                        case OPTION_CONNECT_TIMEOUT:
                                option_connect_timeout = atof(optarg);
                                break;
                        case OPTION_READ_TIMEOUT:
                                option_read_timeout = atof(optarg);
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

        medusa_httprequest_init_options_default(&httprequest_init_options);
        httprequest_init_options.monitor = monitor;
        httprequest_init_options.onevent = httprequest_onevent;
        httprequest_init_options.context = NULL;

        httprequest = medusa_httprequest_create_with_options(&httprequest_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(httprequest)) {
                fprintf(stderr, "can not create httprequest\n");
                goto bail;
        }
        rc = medusa_httprequest_set_connect_timeout(httprequest, option_connect_timeout);
        if (rc != 0) {
                fprintf(stderr, "can not set httprequest connect timeout\n");
                goto bail;
        }
        rc = medusa_httprequest_set_read_timeout(httprequest, option_read_timeout);
        if (rc != 0) {
                fprintf(stderr, "can not set httprequest read timeout\n");
                goto bail;
        }
        rc = medusa_httprequest_set_method(httprequest, option_method);
        if (rc != 0) {
                fprintf(stderr, "can not set httprequest method\n");
                goto bail;
        }
        rc = medusa_httprequest_set_url(httprequest, "%s", option_url);
        if (rc != 0) {
                fprintf(stderr, "can not set httprequest url\n");
                goto bail;
        }

        optind = 0;
        for (_argc = 0; _argc < argc; _argc++) {
                _argv[_argc] = argv[_argc];
        }
        while ((c = getopt_long(_argc, _argv, ":e:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HEADER:
                                rc = medusa_httprequest_add_header(httprequest, optarg, NULL);
                                break;
                }
        }

        rc = medusa_httprequest_make_request(httprequest, option_data, (option_data) ? (strlen(option_data) + 1) : 0);
        if (rc < 0) {
                fprintf(stderr, "can not make post\n");
                goto bail;
        }

        while (1) {
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
