
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

#define OPTIONS_DEFAULT_URL                     "http://127.0.0.1"
#define OPTIONS_DEFAULT_METHOD                  "get"
#define OPTIONS_DEFAULT_DATA                    NULL
#define OPTIONS_DEFAULT_CONNECT_TIMEOUT         5.0
#define OPTIONS_DEFAULT_READ_TIMEOUT            5.0
#define OPTIONS_DEFAULT_EXIT_ON_REQUEST         0

#define OPTIONS_DEFAULT_MEDUSA_MONITOR_POLL     MEDUSA_MONITOR_POLL_DEFAULT
#define OPTIONS_DEFAULT_MEDUSA_MONITOR_SIGNAL   MEDUSA_MONITOR_SIGNAL_DEFAULT
#define OPTIONS_DEFAULT_MEDUSA_MONITOR_TIMER    MEDUSA_MONITOR_TIMER_DEFAULT

#define OPTION_HELP                             'h'
#define OPTION_URL                              'u'
#define OPTION_METHOD                           'm'
#define OPTION_HEADER                           'H'
#define OPTION_DATA                             'd'
#define OPTION_CONNECT_TIMEOUT                  'c'
#define OPTION_READ_TIMEOUT                     'r'
#define OPTION_EXIT_ON_REQUEST                  'R'

#define OPTION_MEDUSA_MONITOR_OPTION_POLL       0x100
#define OPTION_MEDUSA_MONITOR_OPTION_SIGNAL     0x101
#define OPTION_MEDUSA_MONITOR_OPTION_TIMER      0x102

static struct option longopts[] = {
        { "help",                       no_argument,            NULL,        OPTION_HELP                        },
        { "url",                        required_argument,      NULL,        OPTION_URL                         },
        { "method",                     required_argument,      NULL,        OPTION_METHOD                      },
        { "header",                     required_argument,      NULL,        OPTION_HEADER                      },
        { "data",                       required_argument,      NULL,        OPTION_DATA                        },
        { "connect-timeout",            required_argument,      NULL,        OPTION_CONNECT_TIMEOUT             },
        { "read-timeout",               required_argument,      NULL,        OPTION_READ_TIMEOUT                },
        { "exit-on-request",            required_argument,      NULL,        OPTION_EXIT_ON_REQUEST             },
        { "medusa-monitor-poll",        required_argument,      NULL,        OPTION_MEDUSA_MONITOR_OPTION_POLL  },
        { "medusa-monitor-signal",      required_argument,      NULL,        OPTION_MEDUSA_MONITOR_OPTION_SIGNAL},
        { "medusa-monitor-timer",       required_argument,      NULL,        OPTION_MEDUSA_MONITOR_OPTION_TIMER },
        { NULL,                         0,                      NULL,        0                                  },
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
        fprintf(stdout, "  -H, --header: add header\n");
        fprintf(stdout, "  -d, --data  : request data (default: %s)\n", (OPTIONS_DEFAULT_DATA) ? OPTIONS_DEFAULT_DATA : "(null)");
        fprintf(stdout, "  -c, --connect-timeout: connect timeout (default: %.2f)\n", OPTIONS_DEFAULT_CONNECT_TIMEOUT);
        fprintf(stdout, "  -r, --read-timeout   : read timeout (default: %.2f)\n", OPTIONS_DEFAULT_READ_TIMEOUT);
        fprintf(stdout, "  -R, --exit-on-request: exit on request (default: %d)\n", OPTIONS_DEFAULT_EXIT_ON_REQUEST);
        fprintf(stdout, "\n");
        fprintf(stdout, "      --medusa-monitor-poll  : medusa monitor poll type (default: %d)\n", OPTIONS_DEFAULT_MEDUSA_MONITOR_POLL);
        fprintf(stdout, "                               default  : MEDUSA_MONITOR_POLL_DEFAULT\n");
        fprintf(stdout, "                               epoll    : MEDUSA_MONITOR_POLL_EPOLL\n");
        fprintf(stdout, "                               kqueue   : MEDUSA_MONITOR_POLL_KQUEUE\n");
        fprintf(stdout, "                               poll     : MEDUSA_MONITOR_POLL_POLL\n");
        fprintf(stdout, "                               select   : MEDUSA_MONITOR_POLL_SELECT\n");
        fprintf(stdout, "                               wsapoll  : MEDUSA_MONITOR_POLL_WSAPOLL\n");
        fprintf(stdout, "      --medusa-monitor-signal: medusa monitor signal type (default: %d)\n", OPTIONS_DEFAULT_MEDUSA_MONITOR_SIGNAL);
        fprintf(stdout, "                               default  : MEDUSA_MONITOR_SIGNAL_DEFAULT\n");
        fprintf(stdout, "                               sigaction: MEDUSA_MONITOR_SIGNAL_SIGACTION\n");
        fprintf(stdout, "                               null     : MEDUSA_MONITOR_SIGNAL_NULL\n");
        fprintf(stdout, "      --medusa-monitor-timer : medusa monitor timer type (default: %d)\n", OPTIONS_DEFAULT_MEDUSA_MONITOR_TIMER);
        fprintf(stdout, "                               default  : MEDUSA_MONITOR_TIMER_DEFAULT\n");
        fprintf(stdout, "                               timerfd  : MEDUSA_MONITOR_TIMER_TIMERFD\n");
        fprintf(stdout, "                               monotonic: MEDUSA_MONITOR_TIMER_MONOTONIC\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "  -h, --help  : this text\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "example:\n");
        fprintf(stdout, "  %s -u http://127.0.0.1/ -m get -H 'a:b' -H 'c:d'\n", pname);
        fprintf(stdout, "  %s -u http://127.0.0.1/ -m head -H 'a:b' -H 'c:d'\n", pname);
        fprintf(stdout, "  %s -u http://127.0.0.1/ -m post -H 'a:b' -H 'c:d' -d 'data'\n", pname);
}

struct http_request_context {
        int exit_on_request;
};

static int httprequest_onevent (struct medusa_httprequest *httprequest, unsigned int events, void *context, void *param)
{
        struct http_request_context *http_request_context = context;
        (void) httprequest;
        (void) events;
        (void) param;
        fprintf(stderr, "httprequest state: %d, %-35s events: 0x%08x, %s\n", medusa_httprequest_get_state(httprequest), medusa_httprequest_state_string(medusa_httprequest_get_state(httprequest)), events, medusa_httprequest_event_string(events));
        if (events & MEDUSA_HTTPREQUEST_EVENT_REQUESTED) {
                if (http_request_context->exit_on_request) {
                        medusa_monitor_break(medusa_httprequest_get_monitor(httprequest));
                }
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_RECEIVED_STATUS) {
                const struct medusa_httprequest_reply *httprequest_reply;
                const struct medusa_httprequest_reply_status *httprequest_reply_status;

                httprequest_reply = medusa_httprequest_get_reply(httprequest);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply)) {
                        fprintf(stderr, "httprequest reply is invalid\n");
                        goto bail;
                }

                httprequest_reply_status = medusa_httprequest_reply_get_status(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_status)) {
                        fprintf(stderr, "httprequest reply status is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "status:\n");
                fprintf(stderr, "  code : %lld\n", (long long int) medusa_httprequest_reply_status_get_code(httprequest_reply_status));
                fprintf(stderr, "  value: %s\n", medusa_httprequest_reply_status_get_value(httprequest_reply_status));
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_RECEIVED_HEADERS) {
                const struct medusa_httprequest_reply *httprequest_reply;
                const struct medusa_httprequest_reply_status *httprequest_reply_status;
                const struct medusa_httprequest_reply_header *httprequest_reply_header;
                const struct medusa_httprequest_reply_headers *httprequest_reply_headers;

                httprequest_reply = medusa_httprequest_get_reply(httprequest);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply)) {
                        fprintf(stderr, "httprequest reply is invalid\n");
                        goto bail;
                }

                httprequest_reply_status = medusa_httprequest_reply_get_status(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_status)) {
                        fprintf(stderr, "httprequest reply status is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "status:\n");
                fprintf(stderr, "  code : %lld\n", (long long int) medusa_httprequest_reply_status_get_code(httprequest_reply_status));
                fprintf(stderr, "  value: %s\n", medusa_httprequest_reply_status_get_value(httprequest_reply_status));

                httprequest_reply_headers = medusa_httprequest_reply_get_headers(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_headers)) {
                        fprintf(stderr, "httprequest reply headers is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "headers: %lld\n", (long long int) medusa_httprequest_reply_headers_get_count(httprequest_reply_headers));
                for (httprequest_reply_header = medusa_httprequest_reply_headers_get_first(httprequest_reply_headers);
                     httprequest_reply_header;
                     httprequest_reply_header = medusa_httprequest_reply_header_get_next(httprequest_reply_header)) {
                        fprintf(stderr, "  %-15s : %s\n",
                                medusa_httprequest_reply_header_get_key(httprequest_reply_header),
                                medusa_httprequest_reply_header_get_value(httprequest_reply_header));
                }
        }
        if (events & MEDUSA_HTTPREQUEST_EVENT_RECEIVED) {
                const struct medusa_httprequest_reply *httprequest_reply;
                const struct medusa_httprequest_reply_status *httprequest_reply_status;
                const struct medusa_httprequest_reply_header *httprequest_reply_header;
                const struct medusa_httprequest_reply_headers *httprequest_reply_headers;
                const struct medusa_httprequest_reply_body *httprequest_reply_body;

                httprequest_reply = medusa_httprequest_get_reply(httprequest);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply)) {
                        fprintf(stderr, "httprequest reply is invalid\n");
                        goto bail;
                }

                httprequest_reply_status = medusa_httprequest_reply_get_status(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_status)) {
                        fprintf(stderr, "httprequest reply status is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "status:\n");
                fprintf(stderr, "  code : %lld\n", (long long int) medusa_httprequest_reply_status_get_code(httprequest_reply_status));
                fprintf(stderr, "  value: %s\n", medusa_httprequest_reply_status_get_value(httprequest_reply_status));

                httprequest_reply_headers = medusa_httprequest_reply_get_headers(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_headers)) {
                        fprintf(stderr, "httprequest reply headers is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "headers: %lld\n", (long long int) medusa_httprequest_reply_headers_get_count(httprequest_reply_headers));
                for (httprequest_reply_header = medusa_httprequest_reply_headers_get_first(httprequest_reply_headers);
                     httprequest_reply_header;
                     httprequest_reply_header = medusa_httprequest_reply_header_get_next(httprequest_reply_header)) {
                        fprintf(stderr, "  %-15s : %s\n",
                                medusa_httprequest_reply_header_get_key(httprequest_reply_header),
                                medusa_httprequest_reply_header_get_value(httprequest_reply_header));
                }

                httprequest_reply_body = medusa_httprequest_reply_get_body(httprequest_reply);
                if (MEDUSA_IS_ERR_OR_NULL(httprequest_reply_body)) {
                        fprintf(stderr, "httprequest reply body is invalid\n");
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
                struct medusa_httprequest_event_error *medusa_httprequest_event_error = (struct medusa_httprequest_event_error *) param;
                fprintf(stderr, "request failed for request, %s\n", medusa_httprequest_event_string(events));
                fprintf(stderr, "  state : %d, error: %d, line: %d, reason: %d\n", medusa_httprequest_event_error->state, medusa_httprequest_event_error->error, medusa_httprequest_event_error->line, medusa_httprequest_event_error->reason);
                if (medusa_httprequest_event_error->reason == MEDUSA_HTTPREQUEST_ERROR_REASON_PARSER) {
                        fprintf(stderr, "  parser.error: %d\n", medusa_httprequest_event_error->u.parser.error);
                }
                if (medusa_httprequest_event_error->reason == MEDUSA_HTTPREQUEST_ERROR_REASON_TCPSOCKET) {
                        fprintf(stderr, "  tcpsocket.state: %d, error: %d, line: %d\n", medusa_httprequest_event_error->u.tcpsocket.state, medusa_httprequest_event_error->u.tcpsocket.error, medusa_httprequest_event_error->u.tcpsocket.line);
                }
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
        int option_method_set;
        int option_data_set;
        const char *option_data;
        int option_exit_on_request;
        double option_connect_timeout;
        double option_read_timeout;

        int option_medusa_monitor_poll;
        int option_medusa_monitor_signal;
        int option_medusa_monitor_timer;

        int rc;
        struct medusa_monitor_init_options monitor_init_options;
        struct medusa_monitor *monitor;

        struct medusa_httprequest_init_options httprequest_init_options;
        struct medusa_httprequest *httprequest;

        struct http_request_context http_request_context;

#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

        monitor = NULL;

        option_url             = OPTIONS_DEFAULT_URL;
        option_method          = OPTIONS_DEFAULT_METHOD;
        option_method_set      = 0;
        option_data            = NULL;
        option_data_set        = 0;
        option_connect_timeout = OPTIONS_DEFAULT_CONNECT_TIMEOUT;
        option_read_timeout    = OPTIONS_DEFAULT_READ_TIMEOUT;
        option_exit_on_request = OPTIONS_DEFAULT_EXIT_ON_REQUEST;

        option_medusa_monitor_poll   = OPTIONS_DEFAULT_MEDUSA_MONITOR_POLL;
        option_medusa_monitor_signal = OPTIONS_DEFAULT_MEDUSA_MONITOR_SIGNAL;
        option_medusa_monitor_timer  = OPTIONS_DEFAULT_MEDUSA_MONITOR_TIMER;

        _argv = malloc(sizeof(char *) * (argc + 1));

        optind = 0;
        for (_argc = 0; _argc < argc; _argc++) {
                _argv[_argc] = argv[_argc];
        }
        while ((c = getopt_long(_argc, _argv, "hu:m:H:d:c:r:R:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HELP:
                                usage(argv[0]);
                                goto out;
                        case OPTION_URL:
                                option_url = optarg;
                                break;
                        case OPTION_METHOD:
                                option_method = optarg;
                                option_method_set = 1;
                                break;
                        case OPTION_HEADER:
                                break;
                        case OPTION_DATA:
                                option_data = optarg;
                                option_data_set = 1;
                                break;
                        case OPTION_CONNECT_TIMEOUT:
                                option_connect_timeout = atof(optarg);
                                break;
                        case OPTION_READ_TIMEOUT:
                                option_read_timeout = atof(optarg);
                                break;
                        case OPTION_EXIT_ON_REQUEST:
                                option_exit_on_request = !!atoi(optarg);
                                break;

                        case OPTION_MEDUSA_MONITOR_OPTION_POLL:
                                option_medusa_monitor_poll = medusa_monitor_poll_type_value(optarg);
                                if (option_medusa_monitor_poll < 0) {
                                        fprintf(stderr, "invalid medusa monitor poll type: %s\n", optarg);
                                        goto bail;
                                }
                                break;
                        case OPTION_MEDUSA_MONITOR_OPTION_SIGNAL:
                                option_medusa_monitor_signal = medusa_monitor_signal_type_value(optarg);
                                if (option_medusa_monitor_signal < 0) {
                                        fprintf(stderr, "invalid medusa monitor signal type: %s\n", optarg);
                                        goto bail;
                                }
                                break;
                        case OPTION_MEDUSA_MONITOR_OPTION_TIMER:
                                option_medusa_monitor_timer = medusa_monitor_timer_type_value(optarg);
                                if (option_medusa_monitor_timer < 0) {
                                        fprintf(stderr, "invalid medusa monitor timer type: %s\n", optarg);
                                        goto bail;
                                }
                                break;

                        default:
                                fprintf(stderr, "invalid option: %s\n", argv[optind - 1]);
                                goto bail;
                }
        }

        if (!option_method_set) {
                if (option_data_set && option_data != NULL && strlen(option_data) > 0) {
                        option_method = "POST";
                } else {
                        option_method = "GET";
                }
        }

        http_request_context.exit_on_request = option_exit_on_request;

        fprintf(stderr, "options:\n");
        fprintf(stderr, "  url                  : %s\n", option_url);
        fprintf(stderr, "  method               : %s\n", option_method);
        fprintf(stderr, "  data                 : %s\n", (option_data) ? option_data : "(null)");
        fprintf(stderr, "  connect timeout      : %.2f\n", option_connect_timeout);
        fprintf(stderr, "  read timeout         : %.2f\n", option_read_timeout);
        fprintf(stderr, "  exit on request      : %d\n", option_exit_on_request);
        fprintf(stderr, "\n");
        fprintf(stderr, "  medusa monitor poll  : %d, %s\n", option_medusa_monitor_poll, medusa_monitor_poll_type_string(option_medusa_monitor_poll));
        fprintf(stderr, "  medusa monitor signal: %d, %s\n", option_medusa_monitor_signal, medusa_monitor_signal_type_string(option_medusa_monitor_signal));
        fprintf(stderr, "  medusa monitor timer : %d, %s\n", option_medusa_monitor_timer, medusa_monitor_timer_type_string(option_medusa_monitor_timer));
        fprintf(stderr, "\n");

        medusa_monitor_init_options_default(&monitor_init_options);
        monitor_init_options.flags              = MEDUSA_MONITOR_FLAG_NONE;
        monitor_init_options.onevent.callback   = NULL;
        monitor_init_options.onevent.context    = NULL;
        monitor_init_options.poll.type          = option_medusa_monitor_poll;
        monitor_init_options.signal.type        = option_medusa_monitor_signal;
        monitor_init_options.timer.type         = option_medusa_monitor_timer;
        monitor = medusa_monitor_create_with_options(&monitor_init_options);
        if (monitor == NULL) {
                fprintf(stderr, "can not create monitor\n");
                goto bail;
        }

        medusa_httprequest_init_options_default(&httprequest_init_options);
        httprequest_init_options.monitor = monitor;
        httprequest_init_options.onevent = httprequest_onevent;
        httprequest_init_options.context = &http_request_context;

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
        while ((c = getopt_long(_argc, _argv, ":H:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HEADER:
                                rc = medusa_httprequest_add_header(httprequest, optarg, NULL);
                                if (rc < 0) {
                                        fprintf(stderr, "can not add header: %s\n", optarg);
                                        goto bail;
                                }
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
