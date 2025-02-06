
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
#endif

#include "medusa/error.h"
#include "medusa/dnsrequest.h"
#include "medusa/signal.h"
#include "medusa/monitor.h"

static int g_running;

#define OPTION_NAMESERVER_DEFAULT       "8.8.8.8"
#define OPTION_PORT_DEFAULT             53
#define OPTION_CODE_DEFAULT             "query"
#define OPTION_TYPE_DEFAULT             "A"
#define OPTION_NAME_DEFAULT             "www.google.com"

#define OPTION_HELP                     'h'
#define OPTION_NAMESERVER               's'
#define OPTION_PORT                     'p'
#define OPTION_CODE                     'c'
#define OPTION_TYPE                     't'
#define OPTION_NAME                     'n'
static struct option longopts[] = {
        { "help",                       no_argument,            NULL,   OPTION_HELP             },
        { "nameserver",                 required_argument,      NULL,   OPTION_NAMESERVER       },
        { "port",                       required_argument,      NULL,   OPTION_PORT             },
        { "code",                       required_argument,      NULL,   OPTION_CODE             },
        { "type",                       required_argument,      NULL,   OPTION_TYPE             },
        { "name",                       required_argument,      NULL,   OPTION_NAME             },
        { NULL,                         0,                      NULL,   0                       },
};

static void usage (const char *pname)
{
        fprintf(stdout, "usage: %s [-s nameserver] [-t type] -n name:\n", pname);
        fprintf(stdout, "  -h, --help      : this text\n");
        fprintf(stdout, "  -s, --nameserver: nameserver address (default: %s)\n", OPTION_NAMESERVER_DEFAULT);
        fprintf(stdout, "  -p, --port      : nameserver port (default: %d)\n", OPTION_PORT_DEFAULT);
        fprintf(stdout, "  -c, --code      : op code (default: %s)\n", OPTION_CODE_DEFAULT);
        fprintf(stdout, "  -t, --type      : record type (default: %s)\n", OPTION_TYPE_DEFAULT);
        fprintf(stdout, "  -n, --name      : nameto lookup (default: %s)\n", OPTION_NAME_DEFAULT);
}

static int dnsrequest_onevent (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param)
{
        const struct medusa_dnsrequest_reply *dnsrequest_reply;
        const struct medusa_dnsrequest_reply_header *dnsrequest_reply_header;
        const struct medusa_dnsrequest_reply_questions *dnsrequest_reply_questions;
        const struct medusa_dnsrequest_reply_question *dnsrequest_reply_question;
        const struct medusa_dnsrequest_reply_answers *dnsrequest_reply_answers;
        const struct medusa_dnsrequest_reply_answer *dnsrequest_reply_answer;

        (void) events;
        (void) context;
        (void) param;

        fprintf(stderr, "dnsrequest onevent: 0x%08x, %-36s, state: %2d, %s\n",
                events, medusa_dnsrequest_event_string(events),
                medusa_dnsrequest_get_state(dnsrequest), medusa_dnsrequest_state_string(medusa_dnsrequest_get_state(dnsrequest)));

        if (events & MEDUSA_DNSREQUEST_EVENT_RECEIVED) {
                dnsrequest_reply = medusa_dnsrequest_get_reply(dnsrequest);
                if (dnsrequest_reply == NULL) {
                        fprintf(stderr, "dnsrequest_reply is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "dnsrequest_reply: %p\n", dnsrequest_reply);

                dnsrequest_reply_header = medusa_dnsrequest_reply_get_header(dnsrequest_reply);
                if (dnsrequest_reply_header == NULL) {
                        fprintf(stderr, "dnsrequest_reply_header is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "  dnsrequest_reply_header: %p\n", dnsrequest_reply_header);
                fprintf(stderr, "      questions_count      : %d\n", medusa_dnsrequest_reply_header_get_questions_count(dnsrequest_reply_header));
                fprintf(stderr, "      answers_count        : %d\n", medusa_dnsrequest_reply_header_get_answers_count(dnsrequest_reply_header));
                fprintf(stderr, "      nameservers_count    : %d\n", medusa_dnsrequest_reply_header_get_nameservers_count(dnsrequest_reply_header));
                fprintf(stderr, "      additional_records   : %d\n", medusa_dnsrequest_reply_header_get_additional_records(dnsrequest_reply_header));
                fprintf(stderr, "      authoritative_result : %d\n", medusa_dnsrequest_reply_header_get_authoritative_result(dnsrequest_reply_header));
                fprintf(stderr, "      truncated_result     : %d\n", medusa_dnsrequest_reply_header_get_truncated_result(dnsrequest_reply_header));
                fprintf(stderr, "      recursion_desired    : %d\n", medusa_dnsrequest_reply_header_get_recursion_desired(dnsrequest_reply_header));
                fprintf(stderr, "      recursion_available  : %d\n", medusa_dnsrequest_reply_header_get_recursion_available(dnsrequest_reply_header));
                fprintf(stderr, "      result_code          : %d, %s\n", medusa_dnsrequest_reply_header_get_result_code(dnsrequest_reply_header), medusa_dnsrequest_reply_header_get_result_code_string(dnsrequest_reply_header));

                dnsrequest_reply_questions = medusa_dnsrequest_reply_get_questions(dnsrequest_reply);
                if (dnsrequest_reply_questions == NULL) {
                        fprintf(stderr, "dnsrequest_reply_questions is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "  dnsrequest_reply_questions: %p\n", dnsrequest_reply_questions);

                for (dnsrequest_reply_question = medusa_dnsrequest_reply_questions_get_first(dnsrequest_reply_questions);
                     dnsrequest_reply_question != NULL;
                     dnsrequest_reply_question = medusa_dnsrequest_reply_question_get_next(dnsrequest_reply_question)) {
                        fprintf(stderr, "    - name : %s\n", medusa_dnsrequest_reply_question_get_name(dnsrequest_reply_question));
                        fprintf(stderr, "      class: %d\n", medusa_dnsrequest_reply_question_get_class(dnsrequest_reply_question));
                        fprintf(stderr, "      type : %d, %s\n", medusa_dnsrequest_reply_question_get_type(dnsrequest_reply_question), medusa_dnsrequest_record_type_string(medusa_dnsrequest_reply_question_get_type(dnsrequest_reply_question)));
                }

                dnsrequest_reply_answers = medusa_dnsrequest_reply_get_answers(dnsrequest_reply);
                if (dnsrequest_reply_answers == NULL) {
                        fprintf(stderr, "dnsrequest_reply_answers is invalid\n");
                        goto bail;
                }
                fprintf(stderr, "  dnsrequest_reply_answers: %p\n", dnsrequest_reply_answers);

                for (dnsrequest_reply_answer = medusa_dnsrequest_reply_answers_get_first(dnsrequest_reply_answers);
                     dnsrequest_reply_answer != NULL;
                     dnsrequest_reply_answer = medusa_dnsrequest_reply_answer_get_next(dnsrequest_reply_answer)) {
                        fprintf(stderr, "    - name : %s\n", medusa_dnsrequest_reply_answer_get_name(dnsrequest_reply_answer));
                        fprintf(stderr, "      class: %d\n", medusa_dnsrequest_reply_answer_get_class(dnsrequest_reply_answer));
                        fprintf(stderr, "      type : %d, %s\n", medusa_dnsrequest_reply_answer_get_type(dnsrequest_reply_answer), medusa_dnsrequest_record_type_string(medusa_dnsrequest_reply_answer_get_type(dnsrequest_reply_answer)));
                        fprintf(stderr, "      ttl  : %d\n", medusa_dnsrequest_reply_answer_get_ttl(dnsrequest_reply_answer));

                        switch (medusa_dnsrequest_reply_answer_get_type(dnsrequest_reply_answer)) {
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_A:
                                        fprintf(stderr, "      address: %s\n", medusa_dnsrequest_reply_answer_a_get_address(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_NS:
                                        fprintf(stderr, "      nsdname: %s\n", medusa_dnsrequest_reply_answer_ns_get_nsdname(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_CNAME:
                                        fprintf(stderr, "      cname: %s\n", medusa_dnsrequest_reply_answer_cname_get_cname(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_PTR:
                                        fprintf(stderr, "      ptr: %s\n", medusa_dnsrequest_reply_answer_ptr_get_ptr(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_MX:
                                        fprintf(stderr, "      preference: %d\n", medusa_dnsrequest_reply_answer_mx_get_preference(dnsrequest_reply_answer));
                                        fprintf(stderr, "      exchange  : %s\n", medusa_dnsrequest_reply_answer_mx_get_exchange(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_TXT:
                                        fprintf(stderr, "      text: %s\n", medusa_dnsrequest_reply_answer_txt_get_text(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA:
                                        fprintf(stderr, "      address: %s\n", medusa_dnsrequest_reply_answer_aaaa_get_address(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_SRV:
                                        fprintf(stderr, "      priority: %d\n", medusa_dnsrequest_reply_answer_srv_get_priority(dnsrequest_reply_answer));
                                        fprintf(stderr, "      weight  : %d\n", medusa_dnsrequest_reply_answer_srv_get_weight(dnsrequest_reply_answer));
                                        fprintf(stderr, "      port    : %d\n", medusa_dnsrequest_reply_answer_srv_get_port(dnsrequest_reply_answer));
                                        fprintf(stderr, "      target  : %s\n", medusa_dnsrequest_reply_answer_srv_get_target(dnsrequest_reply_answer));
                                        break;
                                case MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR:
                                        fprintf(stderr, "      order      : %hu\n", medusa_dnsrequest_reply_answer_naptr_get_order(dnsrequest_reply_answer));
                                        fprintf(stderr, "      preference : %hu\n", medusa_dnsrequest_reply_answer_naptr_get_preference(dnsrequest_reply_answer));
                                        fprintf(stderr, "      flags      : %s\n", medusa_dnsrequest_reply_answer_naptr_get_flags(dnsrequest_reply_answer));
                                        fprintf(stderr, "      services   : %s\n", medusa_dnsrequest_reply_answer_naptr_get_services(dnsrequest_reply_answer));
                                        fprintf(stderr, "      regexp     : %s\n", medusa_dnsrequest_reply_answer_naptr_get_regexp(dnsrequest_reply_answer));
                                        fprintf(stderr, "      replacement: %s\n", medusa_dnsrequest_reply_answer_naptr_get_replacement(dnsrequest_reply_answer));
                                        break;
                        }
                }

                medusa_monitor_break(medusa_dnsrequest_get_monitor(dnsrequest));
        }

        return 0;
bail:   return -1;
}

static int signal_sigint_onevent (struct medusa_signal *signal, unsigned int events, void *context, void *param)
{
        (void) signal;
        (void) events;
        (void) context;
        (void) param;
        return medusa_monitor_break(medusa_signal_get_monitor(signal));
}

int main (int argc, char *argv[])
{
        int rc;
        int err;

        int c;
        const char *option_nameserver;
        int option_port;
        const char *option_code;
        const char *option_type;
        const char *option_name;

        struct medusa_dnsrequest *medusa_dnsrequest;

        struct medusa_signal *medusa_signal;
        struct medusa_signal_init_options medusa_signal_init_options;

        struct medusa_monitor *medusa_monitor;
        struct medusa_monitor_init_options medusa_monitor_init_options;

#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

        err = 0;
        medusa_monitor = NULL;

        option_nameserver       = OPTION_NAMESERVER_DEFAULT;
        option_port             = OPTION_PORT_DEFAULT;
        option_code             = OPTION_CODE_DEFAULT;
        option_type             = OPTION_TYPE_DEFAULT;
        option_name             = OPTION_NAME_DEFAULT;

        g_running = 1;

        while ((c = getopt_long(argc, argv, "hs:p:c:t:n:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HELP:
                                usage(argv[0]);
                                goto out;
                        case OPTION_NAMESERVER:
                                option_nameserver = optarg;
                                break;
                        case OPTION_PORT:
                                option_port = atoi(optarg);
                                break;
                        case OPTION_CODE:
                                option_code = optarg;
                                break;
                        case OPTION_TYPE:
                                option_type = optarg;
                                break;
                        case OPTION_NAME:
                                option_name = optarg;
                                break;
                        default:
                                fprintf(stderr, "unknown option: %d\n", optopt);
                                err = -EINVAL;
                                goto out;
                }
        }

        if (medusa_dnsrequest_record_type_value(option_type) == MEDUSA_DNSREQUEST_RECORD_TYPE_INVALID) {
                fprintf(stderr, "type is invalid\n");
                err = -EINVAL;
                goto out;
        }
        if (medusa_dnsrequest_record_type_value(option_type) == MEDUSA_DNSREQUEST_RECORD_TYPE_UNKNOWN) {
                fprintf(stderr, "type is invalid\n");
                err = -EINVAL;
                goto out;
        }

        fprintf(stderr, "dns-lookup\n");
        fprintf(stderr, "  nameserver: %s\n", option_nameserver);
        fprintf(stderr, "  code      : %s, %d\n", option_code, medusa_dnsrequest_opcode_value(option_code));
        fprintf(stderr, "  type      : %s, %d\n", option_type, medusa_dnsrequest_record_type_value(option_type));
        fprintf(stderr, "  name      : %s\n", option_name);

        rc = medusa_monitor_init_options_default(&medusa_monitor_init_options);
        if (rc < 0) {
                fprintf(stderr, "can not init medusa monitor init default options\n");
                err = rc;
                goto out;
        }
        medusa_monitor = medusa_monitor_create_with_options(&medusa_monitor_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(medusa_monitor)) {
                fprintf(stderr, "can not create medusa monitor: 0x%p\n", medusa_monitor);
                err = MEDUSA_PTR_ERR(medusa_monitor);
                goto out;
        }

        rc = medusa_signal_init_options_default(&medusa_signal_init_options);
        if (rc < 0) {
                fprintf(stderr, "can not init medusa signal init default options\n");
                err = rc;
                goto out;
        }
        medusa_signal_init_options.monitor     = medusa_monitor;
        medusa_signal_init_options.onevent     = signal_sigint_onevent;
        medusa_signal_init_options.context     = NULL;
        medusa_signal_init_options.enabled     = 1;
        medusa_signal_init_options.number      = SIGINT;
        medusa_signal_init_options.singleshot  = 0;
        medusa_signal = medusa_signal_create_with_options(&medusa_signal_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(medusa_signal)) {
                fprintf(stderr, "can not create medusa signal\n");
                err = MEDUSA_PTR_ERR(medusa_signal);
                goto out;
        }

        medusa_dnsrequest = medusa_dnsrequest_create(medusa_monitor, dnsrequest_onevent, NULL);
        if (MEDUSA_IS_ERR_OR_NULL(medusa_dnsrequest)) {
                err = MEDUSA_PTR_ERR(medusa_dnsrequest);
                goto out;
        }
        rc = medusa_dnsrequest_set_nameserver(medusa_dnsrequest, option_nameserver);
        if (rc != 0) {
                err = rc;
                goto out;
        }
        rc = medusa_dnsrequest_set_port(medusa_dnsrequest, option_port);
        if (rc != 0) {
                err = rc;
                goto out;
        }
        rc = medusa_dnsrequest_set_code(medusa_dnsrequest, medusa_dnsrequest_opcode_value(option_code));
        if (rc != 0) {
                err = rc;
                goto out;
        }
        rc = medusa_dnsrequest_set_type(medusa_dnsrequest, medusa_dnsrequest_record_type_value(option_type));
        if (rc != 0) {
                err = rc;
                goto out;
        }
        rc = medusa_dnsrequest_set_name(medusa_dnsrequest, option_name);
        if (rc != 0) {
                err = rc;
                goto out;
        }
        rc = medusa_dnsrequest_lookup(medusa_dnsrequest);
        if (rc != 0) {
                err = rc;
                goto out;
        }

        while (g_running == 1) {
                rc = medusa_monitor_run_once(medusa_monitor);
                fprintf(stderr, "medusa_monitor_run_once: %d\n", rc);
                if (rc < 0) {
                        err = rc;
                        break;
                } else if (rc == 0) {
                        err = 0;
                        break;
                }
        }

out:    if (!MEDUSA_IS_ERR_OR_NULL(medusa_monitor)) {
                medusa_monitor_destroy(medusa_monitor);
        }
        return err;
}
