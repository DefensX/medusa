
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MEDUSA_DEBUG_NAME       "dnsrequest"

#include "../3rdparty/SPCDNS/src/dns.h"
#include "../3rdparty/SPCDNS/src/mappings.h"
#include "../3rdparty/SPCDNS/src/output.h"

#include "debug.h"
#include "error.h"
#include "pool.h"
#include "queue.h"
#include "subject-struct.h"
#include "udpsocket.h"
#include "udpsocket-private.h"
#include "dnsrequest.h"
#include "dnsrequest-private.h"
#include "dnsrequest-struct.h"
#include "monitor-private.h"

#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL                            0
#endif

#define MEDUSA_DNSREQUEST_USE_POOL              1

#if defined(MEDUSA_DNSREQUEST_USE_POOL) && (MEDUSA_DNSREQUEST_USE_POOL == 1)
static struct medusa_pool *g_pool;
#endif

struct medusa_dnsrequest_reply_header {
        int questions;
        int answers;
        int nameservers;
        int additional_records;
        int authoritative_result;
        int truncated_result;
        int recursion_desired;
        int recursion_available;
        int result;
};

struct medusa_dnsrequest_record_generic {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;
};

struct medusa_dnsrequest_record_a {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        char address[INET_ADDRSTRLEN];
};

struct medusa_dnsrequest_record_ns {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        char *nsdname;
};

struct medusa_dnsrequest_record_cname {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        char *cname;
};

struct medusa_dnsrequest_record_ptr {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        char *ptr;
};

struct medusa_dnsrequest_record_mx {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        int preference;
        char *exchange;
};

struct medusa_dnsrequest_record_txt {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        char *text;
};

struct medusa_dnsrequest_record_aaaa {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        char address[INET6_ADDRSTRLEN];
};

struct medusa_dnsrequest_record_srv {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        int priority;
        int weight;
        int port;
        char *target;
};

struct medusa_dnsrequest_record_naptr {
        char *name;
        unsigned int type;
        unsigned int class;
        unsigned int ttl;

        unsigned short order;
        unsigned short preference;
        char *flags;
        char *services;
        char *regexp;
        char *replacement;
};

TAILQ_HEAD(medusa_dnsrequest_reply_questions, medusa_dnsrequest_reply_question);
struct medusa_dnsrequest_reply_question {
        TAILQ_ENTRY(medusa_dnsrequest_reply_question) list;
        char *name;
        unsigned int type;
        unsigned int class;
};

TAILQ_HEAD(medusa_dnsrequest_reply_answers, medusa_dnsrequest_reply_answer);
struct medusa_dnsrequest_reply_answer {
        TAILQ_ENTRY(medusa_dnsrequest_reply_answer) list;
        union {
                struct medusa_dnsrequest_record_generic generic;
                struct medusa_dnsrequest_record_a       a;
                struct medusa_dnsrequest_record_ns      ns;
                struct medusa_dnsrequest_record_cname   cname;
                struct medusa_dnsrequest_record_ptr     ptr;
                struct medusa_dnsrequest_record_mx      mx;
                struct medusa_dnsrequest_record_txt     txt;
                struct medusa_dnsrequest_record_aaaa    aaaa;
                struct medusa_dnsrequest_record_srv     srv;
                struct medusa_dnsrequest_record_naptr   naptr;
        } u;
};

struct medusa_dnsrequest_reply {
        struct medusa_dnsrequest_reply_header header;
        struct medusa_dnsrequest_reply_questions questions;
        struct medusa_dnsrequest_reply_answers answers;
};

static void medusa_dnsrequest_record_generic_uninit (struct medusa_dnsrequest_record_generic *generic)
{
        if (generic == NULL) {
                return;
        }
        if (generic->name != NULL) {
                free(generic->name);
        }
}

static void medusa_dnsrequest_record_a_uninit (struct medusa_dnsrequest_record_a *a)
{
        if (a == NULL) {
                return;
        }
        memset(a->address, 0, sizeof(a->address));
}

static int medusa_dnsrequest_record_a_init (struct medusa_dnsrequest_record_a *a, dns_a_t *da)
{
        if (a == NULL) {
                goto bail;
        }
        if (da == NULL) {
                goto bail;
        }
        inet_ntop(AF_INET, &da->address, a->address, sizeof(a->address));
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_a_copy (struct medusa_dnsrequest_record_a *a, const struct medusa_dnsrequest_record_a *src)
{
        if (a == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        memcpy(a->address, src->address, sizeof(src->address));
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_record_ns_uninit (struct medusa_dnsrequest_record_ns *ns)
{
        if (ns == NULL) {
                return;
        }
        if (ns->nsdname != NULL) {
                free(ns->nsdname);
                ns->nsdname = NULL;
        }
}

static int medusa_dnsrequest_record_ns_init (struct medusa_dnsrequest_record_ns *ns, dns_ns_t *dns)
{
        if (ns == NULL) {
                goto bail;
        }
        if (dns == NULL) {
                goto bail;
        }
        if (dns->nsdname != NULL) {
                ns->nsdname = strdup(dns->nsdname);
                if (ns->nsdname == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_ns_copy (struct medusa_dnsrequest_record_ns *ns, const struct medusa_dnsrequest_record_ns *src)
{
        if (ns == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        if (src->nsdname != NULL) {
                ns->nsdname = strdup(src->nsdname);
                if (ns->nsdname == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_record_cname_uninit (struct medusa_dnsrequest_record_cname *cname)
{
        if (cname == NULL) {
                return;
        }
        if (cname->cname != NULL) {
                free(cname->cname);
                cname->cname = NULL;
        }
}

static int medusa_dnsrequest_record_cname_init (struct medusa_dnsrequest_record_cname *cname, dns_cname_t *dcname)
{
        if (cname == NULL) {
                goto bail;
        }
        if (dcname == NULL) {
                goto bail;
        }
        if (dcname->cname != NULL) {
                cname->cname = strdup(dcname->cname);
                if (cname->cname == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_cname_copy (struct medusa_dnsrequest_record_cname *cname, const struct medusa_dnsrequest_record_cname *src)
{
        if (cname == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        if (src->cname != NULL) {
                cname->cname = strdup(src->cname);
                if (cname->cname == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_record_ptr_uninit (struct medusa_dnsrequest_record_ptr *ptr)
{
        if (ptr == NULL) {
                return;
        }
        if (ptr->ptr != NULL) {
                free(ptr->ptr);
                ptr->ptr = NULL;
        }
}

static int medusa_dnsrequest_record_ptr_init (struct medusa_dnsrequest_record_ptr *ptr, dns_ptr_t *dptr)
{
        if (ptr == NULL) {
                goto bail;
        }
        if (dptr == NULL) {
                goto bail;
        }
        if (dptr->ptr != NULL) {
                ptr->ptr = strdup(dptr->ptr);
                if (ptr->ptr == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_ptr_copy (struct medusa_dnsrequest_record_ptr *ptr, const struct medusa_dnsrequest_record_ptr *src)
{
        if (ptr == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        if (src->ptr != NULL) {
                ptr->ptr = strdup(src->ptr);
                if (ptr->ptr == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_record_mx_uninit (struct medusa_dnsrequest_record_mx *mx)
{
        if (mx == NULL) {
                return;
        }
        mx->preference = 0;
        if (mx->exchange != NULL) {
                free(mx->exchange);
                mx->exchange = NULL;
        }
}

static int medusa_dnsrequest_record_mx_init (struct medusa_dnsrequest_record_mx *mx, dns_mx_t *dmx)
{
        if (mx == NULL) {
                goto bail;
        }
        if (dmx == NULL) {
                goto bail;
        }
        mx->preference = dmx->preference;
        if (dmx->exchange != NULL) {
                mx->exchange = strdup(dmx->exchange);
                if (mx->exchange == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_mx_copy (struct medusa_dnsrequest_record_mx *mx, const struct medusa_dnsrequest_record_mx *src)
{
        if (mx == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        mx->preference = src->preference;
        if (src->exchange != NULL) {
                mx->exchange = strdup(src->exchange);
                if (mx->exchange == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_record_txt_uninit (struct medusa_dnsrequest_record_txt *txt)
{
        if (txt == NULL) {
                return;
        }
        if (txt->text) {
                free(txt->text);
                txt->text = NULL;
        }
}

static int medusa_dnsrequest_record_txt_init (struct medusa_dnsrequest_record_txt *txt, dns_txt_t *dtxt)
{
        if (txt == NULL) {
                goto bail;
        }
        if (dtxt == NULL) {
                goto bail;
        }
        txt->class    = dtxt->class;
        txt->ttl      = dtxt->ttl;
        txt->type     = dtxt->type;
        if (dtxt->text) {
                txt->text = strdup(dtxt->text);
                if (txt->text == NULL) {
                        goto bail;
                }
        }
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_txt_copy (struct medusa_dnsrequest_record_txt *txt, const struct medusa_dnsrequest_record_txt *src)
{
        if (txt == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        txt->class    = src->class;
        txt->ttl      = src->ttl;
        txt->type     = src->type;
        if (src->text) {
                txt->text = strdup(src->text);
                if (txt->text == NULL) {
                        goto bail;
                }
        }
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_record_aaaa_uninit (struct medusa_dnsrequest_record_aaaa *aaaa)
{
        if (aaaa == NULL) {
                return;
        }
        memset(aaaa->address, 0, sizeof(aaaa->address));
}

static int medusa_dnsrequest_record_aaaa_init (struct medusa_dnsrequest_record_aaaa *aaaa, dns_aaaa_t *daaaa)
{
        if (aaaa == NULL) {
                goto bail;
        }
        if (daaaa == NULL) {
                goto bail;
        }
        inet_ntop(AF_INET6, &daaaa->address, aaaa->address, sizeof(aaaa->address));
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_aaaa_copy (struct medusa_dnsrequest_record_aaaa *aaaa, const struct medusa_dnsrequest_record_aaaa *src)
{
        if (aaaa == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        inet_ntop(AF_INET6, &src->address, aaaa->address, sizeof(aaaa->address));
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_record_srv_uninit (struct medusa_dnsrequest_record_srv *srv)
{
        if (srv == NULL) {
                return;
        }
        srv->priority = 0;
        srv->weight   = 0;
        srv->port     = 0;
        if (srv->target != NULL) {
                free(srv->target);
                srv->target = NULL;
        }
}

static int medusa_dnsrequest_record_srv_init (struct medusa_dnsrequest_record_srv *srv, dns_srv_t *dsrv)
{
        if (srv == NULL) {
                goto bail;
        }
        if (dsrv == NULL) {
                goto bail;
        }
        srv->priority = dsrv->priority;
        srv->weight   = dsrv->weight;
        srv->port     = dsrv->port;
        if (dsrv->target != NULL) {
                srv->target = strdup(dsrv->target);
                if (srv->target == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_srv_copy (struct medusa_dnsrequest_record_srv *srv, const struct medusa_dnsrequest_record_srv *src)
{
        if (srv == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        srv->priority = src->priority;
        srv->weight   = src->weight;
        srv->port     = src->port;
        if (src->target != NULL) {
                srv->target = strdup(src->target);
                if (srv->target == NULL) {
                        goto bail;

                }
        }
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_record_naptr_uninit (struct medusa_dnsrequest_record_naptr *naptr)
{
        if (naptr == NULL) {
                return;
        }
        if (naptr->flags) {
                free(naptr->flags);
                naptr->flags = NULL;
        }
        if (naptr->services) {
                free(naptr->services);
                naptr->services = NULL;
        }
        if (naptr->regexp) {
                free(naptr->regexp);
                naptr->regexp = NULL;
        }
        if (naptr->replacement) {
                free(naptr->replacement);
                naptr->replacement = NULL;
        }
}

static int medusa_dnsrequest_record_naptr_init (struct medusa_dnsrequest_record_naptr *naptr, dns_naptr_t *dnaptr)
{
        if (naptr == NULL) {
                goto bail;
        }
        if (dnaptr == NULL) {
                goto bail;
        }
        naptr->class    = dnaptr->class;
        naptr->ttl      = dnaptr->ttl;
        naptr->type     = dnaptr->type;
        naptr->order    = dnaptr->order;
        naptr->preference       = dnaptr->preference;
        if (dnaptr->flags) {
                naptr->flags = strdup(dnaptr->flags);
                if (naptr->flags == NULL) {
                        goto bail;
                }
        }
        if (dnaptr->services) {
                naptr->services = strdup(dnaptr->services);
                if (naptr->services == NULL) {
                        goto bail;
                }
        }
        if (dnaptr->regexp) {
                naptr->regexp = strdup(dnaptr->regexp);
                if (naptr->regexp == NULL) {
                        goto bail;
                }
        }
        if (dnaptr->replacement) {
                naptr->replacement = strdup(dnaptr->replacement);
                if (naptr->replacement == NULL) {
                        goto bail;
                }
        }
        return 0;
bail:   return -1;
}

static int medusa_dnsrequest_record_naptr_copy (struct medusa_dnsrequest_record_naptr *naptr, const struct medusa_dnsrequest_record_naptr *src)
{
        if (naptr == NULL) {
                goto bail;
        }
        if (src == NULL) {
                goto bail;
        }
        naptr->class    = src->class;
        naptr->ttl      = src->ttl;
        naptr->type     = src->type;
        naptr->order    = src->order;
        naptr->preference       = src->preference;
        if (src->flags) {
                naptr->flags = strdup(src->flags);
                if (naptr->flags == NULL) {
                        goto bail;
                }
        }
        if (src->services) {
                naptr->services = strdup(src->services);
                if (naptr->services == NULL) {
                        goto bail;
                }
        }
        if (src->regexp) {
                naptr->regexp = strdup(src->regexp);
                if (naptr->regexp == NULL) {
                        goto bail;
                }
        }
        if (src->replacement) {
                naptr->replacement = strdup(src->replacement);
                if (naptr->replacement == NULL) {
                        goto bail;
                }
        }
        return 0;
bail:   return -1;
}

static void medusa_dnsrequest_reply_answer_destroy (struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return;
        }
        medusa_dnsrequest_record_generic_uninit(&answer->u.generic);
        switch (answer->u.generic.type) {
                case MEDUSA_DNSREQUEST_RECORD_TYPE_A:           medusa_dnsrequest_record_a_uninit(&answer->u.a);                break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_NS:          medusa_dnsrequest_record_ns_uninit(&answer->u.ns);              break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_CNAME:       medusa_dnsrequest_record_cname_uninit(&answer->u.cname);        break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_PTR:         medusa_dnsrequest_record_ptr_uninit(&answer->u.ptr);            break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_MX:          medusa_dnsrequest_record_mx_uninit(&answer->u.mx);              break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_TXT:         medusa_dnsrequest_record_txt_uninit(&answer->u.txt);            break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA:        medusa_dnsrequest_record_aaaa_uninit(&answer->u.aaaa);          break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_SRV:         medusa_dnsrequest_record_srv_uninit(&answer->u.srv);            break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR:       medusa_dnsrequest_record_naptr_uninit(&answer->u.naptr);        break;
        }
        free(answer);
}

static struct medusa_dnsrequest_reply_answer * medusa_dnsrequest_reply_answer_create (dns_answer_t *danswer)
{
        int rc;
        struct medusa_dnsrequest_reply_answer *answer;

        answer = NULL;

        if (danswer == NULL) {
                goto bail;
        }

        answer = malloc(sizeof(struct medusa_dnsrequest_reply_answer));
        if (answer == NULL) {
                goto bail;
        }
        memset(answer, 0, sizeof(struct medusa_dnsrequest_reply_answer));

        if (danswer->generic.name != NULL) {
                answer->u.generic.name = strdup(danswer->generic.name);
                if (answer->u.generic.name == NULL) {
                        goto bail;
                }
        }
        answer->u.generic.type  = danswer->generic.type;
        answer->u.generic.class = danswer->generic.class;
        answer->u.generic.ttl   = danswer->generic.ttl;

        rc = -1;
        switch (answer->u.generic.type) {
                case MEDUSA_DNSREQUEST_RECORD_TYPE_A:           rc = medusa_dnsrequest_record_a_init(&answer->u.a, &danswer->a);             break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_NS:          rc = medusa_dnsrequest_record_ns_init(&answer->u.ns, &danswer->ns);          break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_CNAME:       rc = medusa_dnsrequest_record_cname_init(&answer->u.cname, &danswer->cname); break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_PTR:         rc = medusa_dnsrequest_record_ptr_init(&answer->u.ptr, &danswer->ptr);       break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_MX:          rc = medusa_dnsrequest_record_mx_init(&answer->u.mx, &danswer->mx);          break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_TXT:         rc = medusa_dnsrequest_record_txt_init(&answer->u.txt, &danswer->txt);       break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA:        rc = medusa_dnsrequest_record_aaaa_init(&answer->u.aaaa, &danswer->aaaa);    break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_SRV:         rc = medusa_dnsrequest_record_srv_init(&answer->u.srv, &danswer->srv);       break;
                case MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR:       rc = medusa_dnsrequest_record_naptr_init(&answer->u.naptr, &danswer->naptr); break;
        }
        if (rc != 0) {
                goto bail;
        }

        return answer;
bail:   if (answer != NULL) {
                medusa_dnsrequest_reply_answer_destroy(answer);
        }
        return NULL;
}

static void medusa_dnsrequest_reply_question_destroy (struct medusa_dnsrequest_reply_question *question)
{
        if (question == NULL) {
                return;
        }
        if (question->name != NULL) {
                free(question->name);
        }
        free(question);
}

static struct medusa_dnsrequest_reply_question * medusa_dnsrequest_reply_question_create (dns_question_t *dquestion)
{
        struct medusa_dnsrequest_reply_question *question;

        question = NULL;

        if (dquestion == NULL) {
                goto bail;
        }

        question = malloc(sizeof(struct medusa_dnsrequest_reply_question));
        if (question == NULL) {
                goto bail;
        }
        memset(question, 0, sizeof(struct medusa_dnsrequest_reply_question));

        if (dquestion->name != NULL) {
                question->name = strdup(dquestion->name);
                if (question->name == NULL) {
                        goto bail;
                }
        }
        question->type  = dquestion->type;
        question->class = dquestion->class;

        return question;
bail:   if (question != NULL) {
                medusa_dnsrequest_reply_question_destroy(question);
        }
        return NULL;
}

static void medusa_dnsrequest_reply_answers_uninit (struct medusa_dnsrequest_reply_answers *answers)
{
        struct medusa_dnsrequest_reply_answer *answer;
        struct medusa_dnsrequest_reply_answer *nanswer;
        if (answers == NULL) {
                return;
        }
        TAILQ_FOREACH_SAFE(answer, answers, list, nanswer) {
                TAILQ_REMOVE(answers, answer, list);
                medusa_dnsrequest_reply_answer_destroy(answer);
        }
        memset(answers, 0, sizeof(struct medusa_dnsrequest_reply_answers));
}

static int medusa_dnsrequest_reply_answers_init (struct medusa_dnsrequest_reply_answers *answers, dns_answer_t *danswers, int dnanswers)
{
        int i;
        struct medusa_dnsrequest_reply_answer *answer;

        if (answers == NULL) {
                goto bail;
        }

        memset(answers, 0, sizeof(struct medusa_dnsrequest_reply_answers));
        TAILQ_INIT(answers);

        for (i = 0; i < dnanswers; i++) {
                answer = medusa_dnsrequest_reply_answer_create(&danswers[i]);
                if (answer == NULL) {
                        goto bail;
                }
                TAILQ_INSERT_TAIL(answers, answer, list);
        }

        return 0;
bail:   if (answers != NULL) {
                medusa_dnsrequest_reply_answers_uninit(answers);
        }
        return -1;
}

static void medusa_dnsrequest_reply_questions_uninit (struct medusa_dnsrequest_reply_questions *questions)
{
        struct medusa_dnsrequest_reply_question *question;
        struct medusa_dnsrequest_reply_question *nquestion;
        if (questions == NULL) {
                return;
        }
        TAILQ_FOREACH_SAFE(question, questions, list, nquestion) {
                TAILQ_REMOVE(questions, question, list);
                medusa_dnsrequest_reply_question_destroy(question);
        }
        memset(questions, 0, sizeof(struct medusa_dnsrequest_reply_questions));
}

static int medusa_dnsrequest_reply_questions_init (struct medusa_dnsrequest_reply_questions *questions, dns_question_t *dquestions, int dnquestions)
{
        int i;
        struct medusa_dnsrequest_reply_question *question;

        if (questions == NULL) {
                goto bail;
        }

        memset(questions, 0, sizeof(struct medusa_dnsrequest_reply_questions));
        TAILQ_INIT(questions);

        for (i = 0; i < dnquestions; i++) {
                question = medusa_dnsrequest_reply_question_create(&dquestions[i]);
                if (question == NULL) {
                        goto bail;
                }
                TAILQ_INSERT_TAIL(questions, question, list);
        }

        return 0;
bail:   if (questions != NULL) {
                medusa_dnsrequest_reply_questions_uninit(questions);
        }
        return -1;
}

static void medusa_dnsrequest_reply_header_uninit (struct medusa_dnsrequest_reply_header *header)
{
        if (header == NULL) {
                return;
        }
        memset(header, 0, sizeof(struct medusa_dnsrequest_reply_header));
}

static int medusa_dnsrequest_reply_header_init (struct medusa_dnsrequest_reply_header *header, dns_query_t *query)
{
        if (header == NULL) {
                goto bail;
        }
        if (query == NULL) {
                goto bail;
        }

        memset(header, 0, sizeof(struct medusa_dnsrequest_reply_header));

        header->questions               = query->qdcount;
        header->answers                 = query->ancount;
        header->nameservers             = query->nscount;
        header->additional_records      = query->arcount;
        header->authoritative_result    = query->aa;
        header->truncated_result        = query->tc;
        header->recursion_desired       = query->rd;
        header->recursion_available     = query->ra;
        header->result                  = query->rcode;

        return 0;
bail:   if (header != NULL) {
                medusa_dnsrequest_reply_header_uninit(header);
        }
        return -1;
}

static void medusa_dnsrequest_reply_destroy (struct medusa_dnsrequest_reply *reply)
{
        if (reply == NULL) {
                return;
        }
        medusa_dnsrequest_reply_header_uninit(&reply->header);
        medusa_dnsrequest_reply_questions_uninit(&reply->questions);
        medusa_dnsrequest_reply_answers_uninit(&reply->answers);
        free(reply);

}

static struct medusa_dnsrequest_reply * medusa_dnsrequest_reply_create (dns_query_t *query)
{
        int rc;
        struct medusa_dnsrequest_reply *reply;

        reply = NULL;

        if (query == NULL) {
                goto bail;
        }

        reply = malloc(sizeof(struct medusa_dnsrequest_reply));
        if (reply == NULL) {
                goto bail;
        }
        memset(reply, 0, sizeof(struct medusa_dnsrequest_reply));

        rc = medusa_dnsrequest_reply_header_init(&reply->header, query);
        if (rc != 0) {
                goto bail;
        }
        rc = medusa_dnsrequest_reply_questions_init(&reply->questions, query->questions, query->qdcount);
        if (rc != 0) {
                goto bail;
        }
        rc = medusa_dnsrequest_reply_answers_init(&reply->answers, query->answers, query->ancount);
        if (rc != 0) {
                goto bail;
        }

        return reply;
bail:   if (reply != NULL) {
                medusa_dnsrequest_reply_destroy(reply);
        }
        return NULL;
}

static inline unsigned int dnsrequest_get_state (const struct medusa_dnsrequest *dnsrequest)
{
        return dnsrequest->state;
}

static inline int dnsrequest_set_state (struct medusa_dnsrequest *dnsrequest, unsigned int state, unsigned int error)
{
        int rc;
        unsigned int pstate;
        struct medusa_dnsrequest_event_state_changed medusa_dnsrequest_event_state_changed;

        if (state == MEDUSA_DNSREQUEST_STATE_DISCONNECTED) {
                if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                        medusa_udpsocket_destroy_unlocked(dnsrequest->udpsocket);
                        dnsrequest->udpsocket = NULL;
                }
        }
        if (state == MEDUSA_DNSREQUEST_STATE_ERROR) {
                if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                        medusa_udpsocket_destroy_unlocked(dnsrequest->udpsocket);
                        dnsrequest->udpsocket = NULL;
                }
        }

        pstate = dnsrequest->state;
        dnsrequest->error = error;
        dnsrequest->state = state;

        medusa_dnsrequest_event_state_changed.pstate = pstate;
        medusa_dnsrequest_event_state_changed.state  = dnsrequest->state;
        medusa_dnsrequest_event_state_changed.error  = dnsrequest->error;
        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_STATE_CHANGED, &medusa_dnsrequest_event_state_changed);
        if (rc < 0) {
                return rc;
        }

        return 0;
}

static int dnsrequest_udpsocket_onevent (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_dnsrequest *dnsrequest = (struct medusa_dnsrequest *) context;

        (void) param;

        monitor = medusa_udpsocket_get_monitor(udpsocket);
        medusa_monitor_lock(monitor);

        if (events & MEDUSA_UDPSOCKET_EVENT_RESOLVING) {
                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_RESOLVING, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_RESOLVING, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_RESOLVED) {
                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_RESOLVED, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_RESOLVED, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_CONNECTING) {
                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_CONNECTING, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_CONNECTING, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_CONNECTED) {
                int fd;

                dns_question_t domain;
                dns_query_t    query;
                dns_packet_t   request[DNS_BUFFER_UDP];
                size_t         reqsize;

                if (dnsrequest_get_state(dnsrequest) == MEDUSA_DNSREQUEST_STATE_CONNECTING) {
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_CONNECTED, 0);
                        if (rc < 0) {
                                medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_CONNECTED, NULL);
                        if (rc < 0) {
                                medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }

                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_REQUESTING, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_REQUESTING, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }

                domain.name  = dnsrequest->name;
                domain.type  = dns_type_value(medusa_dnsrequest_record_type_string(dnsrequest->type) + 30);
                domain.class = CLASS_IN;

                query.id          = dnsrequest->id;
                query.query       = true;
                query.opcode      = dns_op_value(medusa_dnsrequest_opcode_string(dnsrequest->code) + 25);
                query.aa          = false;
                query.tc          = false;
                query.rd          = true;
                query.ra          = false;
                query.z           = false;
                query.ad          = false;
                query.cd          = false;
                query.rcode       = RCODE_OKAY;
                query.qdcount     = 1;
                query.questions   = &domain;
                query.ancount     = 0;
                query.answers     = NULL;
                query.nscount     = 0;
                query.nameservers = NULL;
                query.arcount     = 0;
                query.additional  = NULL;

                reqsize = sizeof(request);
                rc      = dns_encode(request, &reqsize, &query);
                if (rc != RCODE_OKAY) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }

                fd = medusa_udpsocket_get_fd_unlocked(udpsocket);
                if (fd < 0) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }
                rc = sendto(fd, (void *) request, reqsize, MSG_NOSIGNAL, NULL, 0);
                if (rc != (int) reqsize) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }

                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_REQUESTED, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_REQUESTED, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_IN) {
                int fd;

                dns_packet_t reply[DNS_BUFFER_UDP];
                size_t       replysize;

                dns_decoded_t  bufresult[DNS_DECODEBUF_8K];
                size_t         bufsize;

                replysize = sizeof(reply);

                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_RECEIVING, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_RECEIVING, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }

                fd = medusa_udpsocket_get_fd_unlocked(udpsocket);
                if (fd < 0) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }
                rc = recvfrom(fd, (void *) reply, replysize, MSG_NOSIGNAL, NULL, 0);
                if (rc <= 0) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }

                bufsize = sizeof(bufresult);
                rc = dns_decode(bufresult, &bufsize, reply, replysize);
                if (rc != RCODE_OKAY) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }

                //dns_print_result((dns_query_t *)bufresult);

                dnsrequest->reply = medusa_dnsrequest_reply_create((dns_query_t *) bufresult);
                if (dnsrequest->reply == NULL) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                                goto bail;
                        }
                }

                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_RECEIVED, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_RECEIVED, dnsrequest->reply);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_DISCONNECTED, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_IN_TIMEOUT) {
                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_DISCONNECTED, 0);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_RECEIVE_TIMEOUT, NULL);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_ERROR) {
                struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                medusa_dnsrequest_event_error.state = dnsrequest->state;
                medusa_dnsrequest_event_error.error = -EIO;
                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                if (rc < 0) {
                        medusa_errorf("dnsrequest_set_state failed, rc: %d", rc);
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                if (rc < 0) {
                        medusa_errorf("medusa_dnsrequest_onevent_unlocked failed, rc: %d", rc);
                        goto bail;
                }
        }

        medusa_monitor_unlock(monitor);
        return 0;
bail:   medusa_monitor_unlock(monitor);
        return -EIO;
}

static int dnsrequest_init_with_options_unlocked (struct medusa_dnsrequest *dnsrequest, const struct medusa_dnsrequest_init_options *options)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return -EINVAL;
        }
        memset(dnsrequest, 0, sizeof(struct medusa_dnsrequest));
        medusa_subject_set_type(&dnsrequest->subject, MEDUSA_SUBJECT_TYPE_DNSREQUEST);
        dnsrequest->subject.monitor = NULL;
        dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_DISCONNECTED, 0);
        dnsrequest->onevent = options->onevent;
        dnsrequest->context = options->context;
        dnsrequest->resolve_timeout = -1;
        dnsrequest->connect_timeout = -1;
        dnsrequest->receive_timeout = -1;
        rc = medusa_monitor_add_unlocked(options->monitor, &dnsrequest->subject);
        if (rc < 0) {
                return rc;
        }
        rc = medusa_dnsrequest_set_port_unlocked(dnsrequest, (options->port == 0) ? 53 : options->port);
        if (rc != 0) {
                return rc;
        }
        if (options->nameserver != NULL) {
                rc = medusa_dnsrequest_set_nameserver_unlocked(dnsrequest, options->nameserver);
                if (rc != 0) {
                        return rc;
                }
        }
        if (options->code != MEDUSA_DNSREQUEST_OPCODE_INVALID) {
                rc = medusa_dnsrequest_set_code_unlocked(dnsrequest, options->code);
                if (rc != 0) {
                        return rc;
                }
        }
        if (options->type != MEDUSA_DNSREQUEST_RECORD_TYPE_INVALID) {
                rc = medusa_dnsrequest_set_type_unlocked(dnsrequest, options->type);
                if (rc != 0) {
                        return rc;
                }
        }
        if (options->name != NULL) {
                rc = medusa_dnsrequest_set_name_unlocked(dnsrequest, options->name);
                if (rc != 0) {
                        return rc;
                }
        }
        if (options->id >= 0) {
                rc = medusa_dnsrequest_set_id_unlocked(dnsrequest, options->id);
                if (rc != 0) {
                        return rc;
                }
        } else {
                rc = medusa_dnsrequest_set_id_unlocked(dnsrequest, rand() & 0xffff);
                if (rc != 0) {
                        return rc;
                }
        }
        if (options->resolve_timeout >= 0) {
                rc = medusa_dnsrequest_set_resolve_timeout_unlocked(dnsrequest, options->resolve_timeout);
                if (rc != 0) {
                        return rc;
                }
        }
        if (options->connect_timeout >= 0) {
                rc = medusa_dnsrequest_set_connect_timeout_unlocked(dnsrequest, options->connect_timeout);
                if (rc != 0) {
                        return rc;
                }
        }
        if (options->receive_timeout >= 0) {
                rc = medusa_dnsrequest_set_receive_timeout_unlocked(dnsrequest, options->receive_timeout);
                if (rc != 0) {
                        return rc;
                }
        }
        if (options->enabled) {
                rc = medusa_dnsrequest_lookup_unlocked(dnsrequest);
                if (rc != 0) {
                        return rc;
                }
        }
        return 0;
}

static void dnsrequest_uninit_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return;
        }
        if (dnsrequest->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&dnsrequest->subject);
        } else {
                medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_DESTROY, NULL);
        }
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_init_options_default (struct medusa_dnsrequest_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_dnsrequest_init_options));
        options->port = 53;
        options->id   = -1;
        options->code = MEDUSA_DNSREQUEST_OPCODE_QUERY;
        options->resolve_timeout = -1;
        options->connect_timeout = -1;
        options->receive_timeout = -1;
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsrequest * medusa_dnsrequest_create_lookup_unlocked (struct medusa_monitor *monitor, const char *nameserver, unsigned int type, const char *name, int (*onevent) (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_dnsrequest *dnsrequest;

        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (nameserver == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (name == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (onevent == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }

        dnsrequest = medusa_dnsrequest_create_unlocked(monitor, onevent, context);
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return dnsrequest;
        }
        rc = medusa_dnsrequest_set_nameserver_unlocked(dnsrequest, nameserver);
        if (rc != 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        rc = medusa_dnsrequest_set_type_unlocked(dnsrequest, type);
        if (rc != 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        rc = medusa_dnsrequest_set_name_unlocked(dnsrequest, name);
        if (rc != 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        rc = medusa_dnsrequest_lookup_unlocked(dnsrequest);
        if (rc != 0) {
                return MEDUSA_ERR_PTR(rc);
        }

        return dnsrequest;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsrequest * medusa_dnsrequest_create_lookup (struct medusa_monitor *monitor, const char *nameserver, unsigned int type, const char *name, int (*onevent) (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_dnsrequest *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_dnsrequest_create_lookup_unlocked(monitor, nameserver, type, name, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}


__attribute__ ((visibility ("default"))) struct medusa_dnsrequest * medusa_dnsrequest_create_unlocked (struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param), void *context)
{
        int rc;
        struct medusa_dnsrequest_init_options options;
        rc = medusa_dnsrequest_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor = monitor;
        options.onevent = onevent;
        options.context = context;
        return medusa_dnsrequest_create_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_dnsrequest * medusa_dnsrequest_create (struct medusa_monitor *monitor, int (*onevent) (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_dnsrequest *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_dnsrequest_create_unlocked(monitor, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsrequest * medusa_dnsrequest_create_with_options_unlocked (const struct medusa_dnsrequest_init_options *options)
{
        int rc;
        struct medusa_dnsrequest *dnsrequest;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_DNSREQUEST_USE_POOL) && (MEDUSA_DNSREQUEST_USE_POOL == 1)
        dnsrequest = medusa_pool_malloc(g_pool);
#else
        dnsrequest = malloc(sizeof(struct medusa_dnsrequest));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(dnsrequest, 0, sizeof(struct medusa_dnsrequest));
        rc = dnsrequest_init_with_options_unlocked(dnsrequest, options);
        if (rc < 0) {
                medusa_dnsrequest_destroy_unlocked(dnsrequest);
                return MEDUSA_ERR_PTR(rc);
        }
        return dnsrequest;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsrequest * medusa_dnsrequest_create_with_options (const struct medusa_dnsrequest_init_options *options)
{
        struct medusa_dnsrequest *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_dnsrequest_create_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_dnsrequest_destroy_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return;
        }
        dnsrequest_uninit_unlocked(dnsrequest);
}

__attribute__ ((visibility ("default"))) void medusa_dnsrequest_destroy (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        medusa_dnsrequest_destroy_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_state_unlocked (const struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        return dnsrequest_get_state(dnsrequest);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_state (const struct medusa_dnsrequest *dnsrequest)
{
        unsigned int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_state_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_resolve_timeout_unlocked (struct medusa_dnsrequest *dnsrequest, double timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        dnsrequest->resolve_timeout = timeout;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_resolve_timeout (struct medusa_dnsrequest *dnsrequest, double timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_resolve_timeout_unlocked(dnsrequest, timeout);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_dnsrequest_get_resolve_timeout_unlocked (const struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        return dnsrequest->resolve_timeout;
}

__attribute__ ((visibility ("default"))) double medusa_dnsrequest_get_resolve_timeout (const struct medusa_dnsrequest *dnsrequest)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_resolve_timeout_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_connect_timeout_unlocked (struct medusa_dnsrequest *dnsrequest, double timeout)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        dnsrequest->connect_timeout = timeout;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_connect_timeout (struct medusa_dnsrequest *dnsrequest, double timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_connect_timeout_unlocked(dnsrequest, timeout);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_dnsrequest_get_connect_timeout_unlocked (const struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        return dnsrequest->connect_timeout;
}

__attribute__ ((visibility ("default"))) double medusa_dnsrequest_get_connect_timeout (const struct medusa_dnsrequest *dnsrequest)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_connect_timeout_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_receive_timeout_unlocked (struct medusa_dnsrequest *dnsrequest, double timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                rc = medusa_udpsocket_set_read_timeout_unlocked(dnsrequest->udpsocket, timeout);
                if (rc < 0) {
                        return rc;
                }
        }
        dnsrequest->receive_timeout = timeout;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_receive_timeout (struct medusa_dnsrequest *dnsrequest, double timeout)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_receive_timeout_unlocked(dnsrequest, timeout);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) double medusa_dnsrequest_get_receive_timeout_unlocked (const struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        return dnsrequest->receive_timeout;
}

__attribute__ ((visibility ("default"))) double medusa_dnsrequest_get_receive_timeout (const struct medusa_dnsrequest *dnsrequest)
{
        double rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_receive_timeout_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_nameserver_unlocked (struct medusa_dnsrequest *dnsrequest, const char *nameserver)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(nameserver)) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                return -EINPROGRESS;
        }
        if (dnsrequest->nameserver != NULL) {
                free(dnsrequest->nameserver);
        }
        dnsrequest->nameserver = strdup(nameserver);
        if (dnsrequest->nameserver == NULL) {
                return -ENOMEM;
        }
        return medusa_monitor_mod_unlocked(&dnsrequest->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_nameserver (struct medusa_dnsrequest *dnsrequest, const char *nameserver)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(nameserver)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_nameserver_unlocked(dnsrequest, nameserver);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_get_nameserver_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsrequest->nameserver;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_get_nameserver (struct medusa_dnsrequest *dnsrequest)
{
        const char *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_nameserver_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_port_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int port)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                return -EINPROGRESS;
        }
        dnsrequest->port = port;
        return medusa_monitor_mod_unlocked(&dnsrequest->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_port (struct medusa_dnsrequest *dnsrequest, unsigned int port)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_port_unlocked(dnsrequest, port);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_port_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        return dnsrequest->port;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_port (struct medusa_dnsrequest *dnsrequest)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_port_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_code_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int code)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                return -EINPROGRESS;
        }
        dnsrequest->code = code;
        return medusa_monitor_mod_unlocked(&dnsrequest->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_code (struct medusa_dnsrequest *dnsrequest, unsigned int code)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_code_unlocked(dnsrequest, code);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_code_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        return dnsrequest->code;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_code (struct medusa_dnsrequest *dnsrequest)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_code_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_type_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int type)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                return -EINPROGRESS;
        }
        dnsrequest->type = type;
        return medusa_monitor_mod_unlocked(&dnsrequest->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_type (struct medusa_dnsrequest *dnsrequest, unsigned int type)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_type_unlocked(dnsrequest, type);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_type_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        return dnsrequest->type;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_type (struct medusa_dnsrequest *dnsrequest)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_type_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_name_unlocked (struct medusa_dnsrequest *dnsrequest, const char *name)
{
        int l;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(name)) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                return -EINPROGRESS;
        }
        if (dnsrequest->name != NULL) {
                free(dnsrequest->name);
        }
        l = strlen(name);
        if (name[l - 1] == '.') {
                dnsrequest->name = strdup(name);
                if (dnsrequest->name == NULL) {
                        return -ENOMEM;
                }
        } else {
                dnsrequest->name = malloc(l + 1 + 1);
                if (dnsrequest->name == NULL) {
                        return -ENOMEM;
                }
                snprintf(dnsrequest->name, l + 1 + 1, "%s.", name);
        }
        return medusa_monitor_mod_unlocked(&dnsrequest->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_name (struct medusa_dnsrequest *dnsrequest, const char *name)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(name)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_name_unlocked(dnsrequest, name);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_get_name_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsrequest->name;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_get_name (struct medusa_dnsrequest *dnsrequest)
{
        const char *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_name_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_id_unlocked (struct medusa_dnsrequest *dnsrequest, int id)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (id < 0x0000 || id > 0xffff) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                return -EINPROGRESS;
        }
        dnsrequest->id = id;
        return medusa_monitor_mod_unlocked(&dnsrequest->subject);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_id (struct medusa_dnsrequest *dnsrequest, int id)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_id_unlocked(dnsrequest, id);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_id_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        return dnsrequest->id;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_id (struct medusa_dnsrequest *dnsrequest)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_id_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_context_unlocked (struct medusa_dnsrequest *dnsrequest, void *context)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        dnsrequest->context = context;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_context (struct medusa_dnsrequest *dnsrequest, void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_context_unlocked(dnsrequest, context);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsrequest_get_context_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsrequest->context;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsrequest_get_context (struct medusa_dnsrequest *dnsrequest)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_context_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_userdata_unlocked (struct medusa_dnsrequest *dnsrequest, void *userdata)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        dnsrequest->userdata = userdata;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_userdata (struct medusa_dnsrequest *dnsrequest, void *userdata)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_set_userdata_unlocked(dnsrequest, userdata);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsrequest_get_userdata_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsrequest->userdata;
}

__attribute__ ((visibility ("default"))) void * medusa_dnsrequest_get_userdata (struct medusa_dnsrequest *dnsrequest)
{
        void *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_userdata_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_userdata_ptr_unlocked (struct medusa_dnsrequest *dnsrequest, void *userdata)
{
        return medusa_dnsrequest_set_userdata_unlocked(dnsrequest, userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_userdata_ptr (struct medusa_dnsrequest *dnsrequest, void *userdata)
{
        return medusa_dnsrequest_set_userdata(dnsrequest, userdata);
}

__attribute__ ((visibility ("default"))) void * medusa_dnsrequest_get_userdata_ptr_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        return medusa_dnsrequest_get_userdata_unlocked(dnsrequest);
}

__attribute__ ((visibility ("default"))) void * medusa_dnsrequest_get_userdata_ptr (struct medusa_dnsrequest *dnsrequest)
{
        return medusa_dnsrequest_get_userdata(dnsrequest);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_userdata_int_unlocked (struct medusa_dnsrequest *dnsrequest, int userdata)
{
        return medusa_dnsrequest_set_userdata_unlocked(dnsrequest, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_userdata_int (struct medusa_dnsrequest *dnsrequest, int userdata)
{
        return medusa_dnsrequest_set_userdata(dnsrequest, (void *) (intptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_userdata_int_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        return (int) (intptr_t) medusa_dnsrequest_get_userdata_unlocked(dnsrequest);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_get_userdata_int (struct medusa_dnsrequest *dnsrequest)
{
        return (int) (intptr_t) medusa_dnsrequest_get_userdata(dnsrequest);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_userdata_uint_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int userdata)
{
        return medusa_dnsrequest_set_userdata_unlocked(dnsrequest, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_set_userdata_uint (struct medusa_dnsrequest *dnsrequest, unsigned int userdata)
{
        return medusa_dnsrequest_set_userdata(dnsrequest, (void *) (uintptr_t) userdata);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_dnsrequest_get_userdata_uint_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        return (unsigned int) (intptr_t) medusa_dnsrequest_get_userdata_unlocked(dnsrequest);
}

__attribute__ ((visibility ("default"))) unsigned int medusa_dnsrequest_get_userdata_uint (struct medusa_dnsrequest *dnsrequest)
{
        return (unsigned int) (uintptr_t) medusa_dnsrequest_get_userdata(dnsrequest);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_lookup_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        int rc;

        struct medusa_udpsocket_connect_options medusa_udpsocket_connect_options;

        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }

        if (dnsrequest->name == NULL) {
                return -EINVAL;
        }
        if (dnsrequest->code == MEDUSA_DNSREQUEST_OPCODE_INVALID) {
                return -EINVAL;
        }
        if (dnsrequest->type == MEDUSA_DNSREQUEST_RECORD_TYPE_INVALID) {
                return -EINVAL;
        }
        if (dnsrequest->type == MEDUSA_DNSREQUEST_RECORD_TYPE_UNKNOWN) {
                return -EINVAL;
        }

        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                rc = medusa_udpsocket_connect_options_default(&medusa_udpsocket_connect_options);
                if (rc != 0) {
                        return rc;
                }
                medusa_udpsocket_connect_options.monitor     = medusa_dnsrequest_get_monitor_unlocked(dnsrequest);
                medusa_udpsocket_connect_options.onevent     = dnsrequest_udpsocket_onevent;
                medusa_udpsocket_connect_options.context     = dnsrequest;
                medusa_udpsocket_connect_options.address     = dnsrequest->nameserver;
                medusa_udpsocket_connect_options.port        = dnsrequest->port;
                medusa_udpsocket_connect_options.protocol    = MEDUSA_UDPSOCKET_PROTOCOL_ANY;
                medusa_udpsocket_connect_options.enabled     = 1;
                medusa_udpsocket_connect_options.nonblocking = 1;
                dnsrequest->udpsocket = medusa_udpsocket_connect_with_options_unlocked(&medusa_udpsocket_connect_options);
                if (MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                        return MEDUSA_PTR_ERR(dnsrequest->udpsocket);
                }
                rc = medusa_udpsocket_set_read_timeout_unlocked(dnsrequest->udpsocket, dnsrequest->receive_timeout);
                if (rc < 0) {
                        medusa_udpsocket_destroy_unlocked(dnsrequest->udpsocket);
                        dnsrequest->udpsocket = NULL;
                        goto bail;
                }
        } else if (dnsrequest_get_state(dnsrequest) == MEDUSA_DNSREQUEST_STATE_CONNECTED ||
                   dnsrequest_get_state(dnsrequest) == MEDUSA_DNSREQUEST_STATE_REQUESTING ||
                   dnsrequest_get_state(dnsrequest) == MEDUSA_DNSREQUEST_STATE_REQUESTED) {
                int fd;

                dns_question_t domain;
                dns_query_t    query;
                dns_packet_t   request[DNS_BUFFER_UDP];
                size_t         reqsize;

                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_REQUESTING, 0);
                if (rc < 0) {
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_REQUESTING, NULL);
                if (rc < 0) {
                        goto bail;
                }

                domain.name  = dnsrequest->name;
                domain.type  = dns_type_value(medusa_dnsrequest_record_type_string(dnsrequest->type) + 30);
                domain.class = CLASS_IN;

                query.id          = dnsrequest->id;
                query.query       = true;
                query.opcode      = dns_op_value(medusa_dnsrequest_opcode_string(dnsrequest->code) + 25);;
                query.aa          = false;
                query.tc          = false;
                query.rd          = true;
                query.ra          = false;
                query.z           = false;
                query.ad          = false;
                query.cd          = false;
                query.rcode       = RCODE_OKAY;
                query.qdcount     = 1;
                query.questions   = &domain;
                query.ancount     = 0;
                query.answers     = NULL;
                query.nscount     = 0;
                query.nameservers = NULL;
                query.arcount     = 0;
                query.additional  = NULL;

                reqsize = sizeof(request);
                rc      = dns_encode(request, &reqsize, &query);
                if (rc != RCODE_OKAY) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                goto bail;
                        }
                }

                fd = medusa_udpsocket_get_fd_unlocked(dnsrequest->udpsocket);
                if (fd < 0) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                goto bail;
                        }
                }
                rc = sendto(fd, (void *) request, reqsize, MSG_NOSIGNAL, NULL, 0);
                if (rc != (int) reqsize) {
                        struct medusa_dnsrequest_event_error medusa_dnsrequest_event_error;
                        medusa_dnsrequest_event_error.state = dnsrequest->state;
                        medusa_dnsrequest_event_error.error = -EIO;
                        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_ERROR, medusa_dnsrequest_event_error.error);
                        if (rc < 0) {
                                goto bail;
                        }
                        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_ERROR, &medusa_dnsrequest_event_error);
                        if (rc < 0) {
                                goto bail;
                        }
                }

                rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_REQUESTED, 0);
                if (rc < 0) {
                        goto bail;
                }
                rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_REQUESTED, NULL);
                if (rc < 0) {
                        goto bail;
                }
        }

        return 0;
bail:   return -EIO;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_lookup (struct medusa_dnsrequest *dnsrequest)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_lookup_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_cancel_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                return -EALREADY;
        }
        rc = dnsrequest_set_state(dnsrequest, MEDUSA_DNSREQUEST_STATE_DISCONNECTED, 0);
        if (rc < 0) {
                return -EIO;
        }
        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, MEDUSA_DNSREQUEST_EVENT_CANCELED, NULL);
        if (rc < 0) {
                return -EIO;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_cancel (struct medusa_dnsrequest *dnsrequest)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_cancel_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_abort_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        return medusa_dnsrequest_cancel_unlocked(dnsrequest);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_abort (struct medusa_dnsrequest *dnsrequest)
{
        return medusa_dnsrequest_cancel(dnsrequest);
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply * medusa_dnsrequest_get_reply_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsrequest->reply;
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply * medusa_dnsrequest_get_reply (struct medusa_dnsrequest *dnsrequest)
{
        const struct medusa_dnsrequest_reply *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_reply_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply_header * medusa_dnsrequest_reply_get_header (const struct medusa_dnsrequest_reply *reply)
{
        if (MEDUSA_IS_ERR_OR_NULL(reply)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return &reply->header;
}

__attribute__ ((visibility ("default"))) struct medusa_dnsrequest_reply_answers * medusa_dnsrequest_reply_answers_copy (const struct medusa_dnsrequest_reply_answers *answers)
{
        int rs;
        struct medusa_dnsrequest_reply_answer *acopy;
        struct medusa_dnsrequest_reply_answers *copy;
        const struct medusa_dnsrequest_reply_answer *answer;

        rs = -EIO;
        copy = NULL;

        if (MEDUSA_IS_ERR_OR_NULL(answers)) {
                rs = -EINVAL;
                goto bail;
        }

        copy = malloc(sizeof(struct medusa_dnsrequest_reply_answers));
        if (copy == NULL) {
                rs = -ENOMEM;
                goto bail;
        }
        TAILQ_INIT(copy);

        for (answer = medusa_dnsrequest_reply_answers_get_first(answers);
             answer != NULL;
             answer = medusa_dnsrequest_reply_answer_get_next(answer)) {
                acopy = malloc(sizeof(struct medusa_dnsrequest_reply_answer));
                if (acopy == NULL) {
                        rs = -ENOMEM;
                        goto bail;
                }
                memset(acopy, 0, sizeof(struct medusa_dnsrequest_reply_answer));
                TAILQ_INSERT_TAIL(copy, acopy, list);

                if (answer->u.generic.name != NULL) {
                        acopy->u.generic.name = strdup(answer->u.generic.name);
                        if (acopy->u.generic.name == NULL) {
                                rs = -ENOMEM;
                                goto bail;
                        }
                }
                acopy->u.generic.type  = answer->u.generic.type;
                acopy->u.generic.class = answer->u.generic.class;
                acopy->u.generic.ttl   = answer->u.generic.ttl;

                rs = -EIO;
                switch (answer->u.generic.type) {
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_A:           rs = medusa_dnsrequest_record_a_copy(&acopy->u.a, &answer->u.a);             break;
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_NS:          rs = medusa_dnsrequest_record_ns_copy(&acopy->u.ns, &answer->u.ns);          break;
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_CNAME:       rs = medusa_dnsrequest_record_cname_copy(&acopy->u.cname, &answer->u.cname); break;
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_PTR:         rs = medusa_dnsrequest_record_ptr_copy(&acopy->u.ptr, &answer->u.ptr);       break;
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_MX:          rs = medusa_dnsrequest_record_mx_copy(&acopy->u.mx, &answer->u.mx);          break;
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_TXT:         rs = medusa_dnsrequest_record_txt_copy(&acopy->u.txt, &answer->u.txt);       break;
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA:        rs = medusa_dnsrequest_record_aaaa_copy(&acopy->u.aaaa, &answer->u.aaaa);    break;
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_SRV:         rs = medusa_dnsrequest_record_srv_copy(&acopy->u.srv, &answer->u.srv);       break;
                        case MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR:       rs = medusa_dnsrequest_record_naptr_copy(&acopy->u.naptr, &answer->u.naptr); break;
                }
                if (rs != 0) {
                        goto bail;
                }
        }

        return copy;

bail:   if (copy != NULL) {
                medusa_dnsrequest_reply_answers_destroy(copy);
        }
        return MEDUSA_ERR_PTR(rs);
}

__attribute__ ((visibility ("default"))) void medusa_dnsrequest_reply_answers_destroy (struct medusa_dnsrequest_reply_answers *answers)
{
        if (answers == NULL) {
                return;
        }
        medusa_dnsrequest_reply_answers_uninit(answers);
        free(answers);
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply_answers * medusa_dnsrequest_reply_get_answers (const struct medusa_dnsrequest_reply *reply)
{
        if (MEDUSA_IS_ERR_OR_NULL(reply)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return &reply->answers;
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply_questions * medusa_dnsrequest_reply_get_questions (const struct medusa_dnsrequest_reply *reply)
{
        if (MEDUSA_IS_ERR_OR_NULL(reply)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return &reply->questions;
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply_answer * medusa_dnsrequest_reply_answers_get_first (const struct medusa_dnsrequest_reply_answers *answers)
{
        if (MEDUSA_IS_ERR_OR_NULL(answers)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return TAILQ_FIRST(answers);
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply_answer * medusa_dnsrequest_reply_answer_get_next (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (MEDUSA_IS_ERR_OR_NULL(answer)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return TAILQ_NEXT(answer, list);
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply_question * medusa_dnsrequest_reply_questions_get_first (const struct medusa_dnsrequest_reply_questions *questions)
{
        if (MEDUSA_IS_ERR_OR_NULL(questions)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return TAILQ_FIRST(questions);
}

__attribute__ ((visibility ("default"))) const struct medusa_dnsrequest_reply_question * medusa_dnsrequest_reply_question_get_next (const struct medusa_dnsrequest_reply_question *question)
{
        if (MEDUSA_IS_ERR_OR_NULL(question)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return TAILQ_NEXT(question, list);
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_questions_count (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->questions;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_answers_count (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->answers;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_nameservers_count (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->nameservers;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_additional_records (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->additional_records;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_authoritative_result (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->authoritative_result;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_truncated_result (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->truncated_result;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_recursion_desired (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->recursion_desired;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_recursion_available (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->recursion_available;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_header_get_result_code (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return -EINVAL;
        }
        return header->result;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_header_get_result_code_string (const struct medusa_dnsrequest_reply_header *header)
{
        if (MEDUSA_IS_ERR_OR_NULL(header)) {
                return NULL;
        }
        switch (header->result) {
                case 0: return "OKAY";
                case 1: return "FORMAT_ERROR";
                case 2: return "SERVER_FAILURE";
                case 3: return "NAME_ERROR";
                case 4: return "NOT_IMPLEMENTED";
                case 5: return "REFUSED";
                case 6: return "YXDOMAIN";
                case 7: return "YXRRSET";
                case 8: return "NXRRSET";
                case 9: return "NOTAUTH";
                case 10: return "NOTZONE";
        }
        return "ERROR";
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_question_get_name (const struct medusa_dnsrequest_reply_question *question)
{
        if (question == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return question->name;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_question_get_class (const struct medusa_dnsrequest_reply_question *question)
{
        if (question == NULL) {
                return -EINVAL;
        }
        return question->class;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_question_get_type (const struct medusa_dnsrequest_reply_question *question)
{
        if (question == NULL) {
                return -EINVAL;
        }
        return question->type;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_get_name (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.generic.name;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_answer_get_class (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        return answer->u.generic.class;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_answer_get_type (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        return answer->u.generic.type;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_answer_get_ttl (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        return answer->u.generic.ttl;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_a_get_address (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_A) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.a.address;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_ns_get_nsdname (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_NS) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.ns.nsdname;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_cname_get_cname (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_CNAME) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.cname.cname;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_ptr_get_ptr (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_PTR) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.ptr.ptr;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_answer_mx_get_preference (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_MX) {
                return -EINVAL;
        }
        return answer->u.mx.preference;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_mx_get_exchange (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_MX) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.mx.exchange;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_txt_get_text (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_TXT) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.txt.text;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_aaaa_get_address (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.aaaa.address;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_answer_srv_get_priority (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_SRV) {
                return -EINVAL;
        }
        return answer->u.srv.priority;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_answer_srv_get_weight (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_SRV) {
                return -EINVAL;
        }
        return answer->u.srv.weight;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_reply_answer_srv_get_port (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_SRV) {
                return -EINVAL;
        }
        return answer->u.srv.port;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_srv_get_target (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_SRV) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.srv.target;
}

__attribute__ ((visibility ("default"))) unsigned short medusa_dnsrequest_reply_answer_naptr_get_order (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR) {
                return -EINVAL;
        }
        return answer->u.naptr.order;
}

__attribute__ ((visibility ("default"))) unsigned short medusa_dnsrequest_reply_answer_naptr_get_preference (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return -EINVAL;
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR) {
                return -EINVAL;
        }
        return answer->u.naptr.preference;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_naptr_get_flags (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.naptr.flags;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_naptr_get_services (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.naptr.services;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_naptr_get_regexp (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.naptr.regexp;
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_reply_answer_naptr_get_replacement (const struct medusa_dnsrequest_reply_answer *answer)
{
        if (answer == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (answer->u.generic.type != MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return answer->u.naptr.replacement;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_onevent_unlocked (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *param)
{
        int ret;
        struct medusa_monitor *monitor;
        ret = 0;
        monitor = dnsrequest->subject.monitor;
        if (dnsrequest->onevent != NULL) {
                if ((medusa_subject_is_active(&dnsrequest->subject)) ||
                    (events & MEDUSA_DNSREQUEST_EVENT_DESTROY)) {
                        medusa_monitor_unlock(monitor);
                        ret = dnsrequest->onevent(dnsrequest, events, dnsrequest->context, param);
                        if (ret < 0) {
                                medusa_errorf("dnsrequest->onevent failed, rc: %d", ret);
                        }
                        medusa_monitor_lock(monitor);
                }
        }
        if (events & MEDUSA_DNSREQUEST_EVENT_DESTROY) {
                if (dnsrequest->nameserver != NULL) {
                        free(dnsrequest->nameserver);
                        dnsrequest->nameserver = NULL;
                }
                if (dnsrequest->name != NULL) {
                        free(dnsrequest->name);
                        dnsrequest->name = NULL;
                }
                if (dnsrequest->reply != NULL) {
                        medusa_dnsrequest_reply_destroy(dnsrequest->reply);
                        dnsrequest->reply = NULL;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(dnsrequest->udpsocket)) {
                        medusa_udpsocket_destroy_unlocked(dnsrequest->udpsocket);
                        dnsrequest->udpsocket = NULL;
                }
#if defined(MEDUSA_DNSREQUEST_USE_POOL) && (MEDUSA_DNSREQUEST_USE_POOL == 1)
                medusa_pool_free(dnsrequest);
#else
                free(dnsrequest);
#endif
        }
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_dnsrequest_onevent (struct medusa_dnsrequest *dnsrequest, unsigned int events, void *param)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return -EINVAL;
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_onevent_unlocked(dnsrequest, events, param);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_dnsrequest_get_monitor_unlocked (struct medusa_dnsrequest *dnsrequest)
{
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return dnsrequest->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_dnsrequest_get_monitor (struct medusa_dnsrequest *dnsrequest)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(dnsrequest)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(dnsrequest->subject.monitor);
        rc = medusa_dnsrequest_get_monitor_unlocked(dnsrequest);
        medusa_monitor_unlock(dnsrequest->subject.monitor);
        return rc;
}

unsigned int medusa_dnsrequest_opcode_value (const char *type)
{
        if (strcasecmp(type, "QUERY") == 0)     return MEDUSA_DNSREQUEST_OPCODE_QUERY;
        if (strcasecmp(type, "IQUERY") == 0)    return MEDUSA_DNSREQUEST_OPCODE_IQUERY;
        if (strcasecmp(type, "STATUS") == 0)    return MEDUSA_DNSREQUEST_OPCODE_STATUS;
        if (strcasecmp(type, "NOTIFY") == 0)    return MEDUSA_DNSREQUEST_OPCODE_NOTIFY;
        if (strcasecmp(type, "UPDATE") == 0)    return MEDUSA_DNSREQUEST_OPCODE_UPDATE;
        if (strcasecmp(type, "UNKNOWN") == 0)   return MEDUSA_DNSREQUEST_OPCODE_UNKNOWN;
        return MEDUSA_DNSREQUEST_RECORD_TYPE_UNKNOWN;
}

const char * medusa_dnsrequest_opcode_string (unsigned int type)
{
        switch (type) {
                case MEDUSA_DNSREQUEST_OPCODE_QUERY:    return "MEDUSA_DNSREQUEST_OPCODE_QUERY";
                case MEDUSA_DNSREQUEST_OPCODE_IQUERY:   return "MEDUSA_DNSREQUEST_OPCODE_IQUERY";
                case MEDUSA_DNSREQUEST_OPCODE_STATUS:   return "MEDUSA_DNSREQUEST_OPCODE_STATUS";
                case MEDUSA_DNSREQUEST_OPCODE_NOTIFY:   return "MEDUSA_DNSREQUEST_OPCODE_NOTIFY";
                case MEDUSA_DNSREQUEST_OPCODE_UPDATE:   return "MEDUSA_DNSREQUEST_OPCODE_UPDATE";
                case MEDUSA_DNSREQUEST_OPCODE_UNKNOWN:  return "MEDUSA_DNSREQUEST_OPCODE_UNKNOWN";
                default:
                        return "MEDUSA_DNSREQUEST_OPCODE_UNKNOWN";
        }
}

unsigned int medusa_dnsrequest_record_type_value (const char *type)
{
        if (strcasecmp(type, "INVALID") == 0)   return MEDUSA_DNSREQUEST_RECORD_TYPE_INVALID;
        if (strcasecmp(type, "A") == 0)         return MEDUSA_DNSREQUEST_RECORD_TYPE_A;
        if (strcasecmp(type, "NS") == 0)        return MEDUSA_DNSREQUEST_RECORD_TYPE_NS;
        if (strcasecmp(type, "CNAME") == 0)     return MEDUSA_DNSREQUEST_RECORD_TYPE_CNAME;
        if (strcasecmp(type, "PTR") == 0)       return MEDUSA_DNSREQUEST_RECORD_TYPE_PTR;
        if (strcasecmp(type, "MX") == 0)        return MEDUSA_DNSREQUEST_RECORD_TYPE_MX;
        if (strcasecmp(type, "TXT") == 0)       return MEDUSA_DNSREQUEST_RECORD_TYPE_TXT;
        if (strcasecmp(type, "AAAA") == 0)      return MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA;
        if (strcasecmp(type, "SRV") == 0)       return MEDUSA_DNSREQUEST_RECORD_TYPE_SRV;
        if (strcasecmp(type, "NAPTR") == 0)     return MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR;
        if (strcasecmp(type, "HTTPS") == 0)     return MEDUSA_DNSREQUEST_RECORD_TYPE_HTTPS;
        if (strcasecmp(type, "ANY") == 0)       return MEDUSA_DNSREQUEST_RECORD_TYPE_ANY;
        if (strcasecmp(type, "UNKNOWN") == 0)   return MEDUSA_DNSREQUEST_RECORD_TYPE_UNKNOWN;
        return MEDUSA_DNSREQUEST_RECORD_TYPE_UNKNOWN;
}

const char * medusa_dnsrequest_record_type_string (unsigned int type)
{
        switch (type) {
                case MEDUSA_DNSREQUEST_RECORD_TYPE_INVALID:     return "MEDUSA_DNSREQUEST_RECORD_TYPE_INVALID";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_A:           return "MEDUSA_DNSREQUEST_RECORD_TYPE_A";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_NS:          return "MEDUSA_DNSREQUEST_RECORD_TYPE_NS";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_CNAME:       return "MEDUSA_DNSREQUEST_RECORD_TYPE_CNAME";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_PTR:         return "MEDUSA_DNSREQUEST_RECORD_TYPE_PTR";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_MX:          return "MEDUSA_DNSREQUEST_RECORD_TYPE_MX";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_TXT:         return "MEDUSA_DNSREQUEST_RECORD_TYPE_TXT";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA:        return "MEDUSA_DNSREQUEST_RECORD_TYPE_AAAA";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_SRV:         return "MEDUSA_DNSREQUEST_RECORD_TYPE_SRV";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR:       return "MEDUSA_DNSREQUEST_RECORD_TYPE_NAPTR";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_HTTPS:       return "MEDUSA_DNSREQUEST_RECORD_TYPE_HTTPS";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_ANY:         return "MEDUSA_DNSREQUEST_RECORD_TYPE_ANY";
                case MEDUSA_DNSREQUEST_RECORD_TYPE_UNKNOWN:     return "MEDUSA_DNSREQUEST_RECORD_TYPE_UNKNOWN";
                default:
                        return "MEDUSA_DNSREQUEST_RECORD_TYPE_UNKNOWN";
        }
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_event_string (unsigned int events)
{
        if (events == MEDUSA_DNSREQUEST_EVENT_RESOLVING)        return "MEDUSA_DNSREQUEST_EVENT_RESOLVING";
        if (events == MEDUSA_DNSREQUEST_EVENT_RESOLVE_TIMEOUT)  return "MEDUSA_DNSREQUEST_EVENT_RESOLVE_TIMEOUT";
        if (events == MEDUSA_DNSREQUEST_EVENT_RESOLVED)         return "MEDUSA_DNSREQUEST_EVENT_RESOLVED";
        if (events == MEDUSA_DNSREQUEST_EVENT_CONNECTING)       return "MEDUSA_DNSREQUEST_EVENT_CONNECTING";
        if (events == MEDUSA_DNSREQUEST_EVENT_CONNECT_TIMEOUT)  return "MEDUSA_DNSREQUEST_EVENT_CONNECT_TIMEOUT";
        if (events == MEDUSA_DNSREQUEST_EVENT_CONNECTED)        return "MEDUSA_DNSREQUEST_EVENT_CONNECTED";
        if (events == MEDUSA_DNSREQUEST_EVENT_REQUESTING)       return "MEDUSA_DNSREQUEST_EVENT_REQUESTING";
        if (events == MEDUSA_DNSREQUEST_EVENT_REQUESTED)        return "MEDUSA_DNSREQUEST_EVENT_REQUESTED";
        if (events == MEDUSA_DNSREQUEST_EVENT_RECEIVING)        return "MEDUSA_DNSREQUEST_EVENT_RECEIVING";
        if (events == MEDUSA_DNSREQUEST_EVENT_RECEIVED)         return "MEDUSA_DNSREQUEST_EVENT_RECEIVED";
        if (events == MEDUSA_DNSREQUEST_EVENT_RECEIVE_TIMEOUT)  return "MEDUSA_DNSREQUEST_EVENT_RECEIVE_TIMEOUT";
        if (events == MEDUSA_DNSREQUEST_EVENT_CANCELED)         return "MEDUSA_DNSREQUEST_EVENT_CANCELED";
        if (events == MEDUSA_DNSREQUEST_EVENT_ERROR)            return "MEDUSA_DNSREQUEST_EVENT_ERROR";
        if (events == MEDUSA_DNSREQUEST_EVENT_DISCONNECTED)     return "MEDUSA_DNSREQUEST_EVENT_DISCONNECTED";
        if (events == MEDUSA_DNSREQUEST_EVENT_STATE_CHANGED)    return "MEDUSA_DNSREQUEST_EVENT_STATE_CHANGED";
        if (events == MEDUSA_DNSREQUEST_EVENT_DESTROY)          return "MEDUSA_DNSREQUEST_EVENT_DESTROY";
        return "MEDUSA_DNSREQUEST_EVENT_UNKNOWN";
}

__attribute__ ((visibility ("default"))) const char * medusa_dnsrequest_state_string (unsigned int state)
{
        if (state == MEDUSA_DNSREQUEST_STATE_UNKNOWN)           return "MEDUSA_DNSREQUEST_STATE_UNKNOWN";
        if (state == MEDUSA_DNSREQUEST_STATE_DISCONNECTED)      return "MEDUSA_DNSREQUEST_STATE_DISCONNECTED";
        if (state == MEDUSA_DNSREQUEST_STATE_RESOLVING)         return "MEDUSA_DNSREQUEST_STATE_RESOLVING";
        if (state == MEDUSA_DNSREQUEST_STATE_RESOLVED)          return "MEDUSA_DNSREQUEST_STATE_RESOLVED";
        if (state == MEDUSA_DNSREQUEST_STATE_CONNECTING)        return "MEDUSA_DNSREQUEST_STATE_CONNECTING";
        if (state == MEDUSA_DNSREQUEST_STATE_CONNECTED)         return "MEDUSA_DNSREQUEST_STATE_CONNECTED";
        if (state == MEDUSA_DNSREQUEST_STATE_REQUESTING)        return "MEDUSA_DNSREQUEST_STATE_REQUESTING";
        if (state == MEDUSA_DNSREQUEST_STATE_REQUESTED)         return "MEDUSA_DNSREQUEST_STATE_REQUESTED";
        if (state == MEDUSA_DNSREQUEST_STATE_RECEIVING)         return "MEDUSA_DNSREQUEST_STATE_RECEIVING";
        if (state == MEDUSA_DNSREQUEST_STATE_RECEIVED)          return "MEDUSA_DNSREQUEST_STATE_RECEIVED";
        if (state == MEDUSA_DNSREQUEST_STATE_ERROR)             return "MEDUSA_DNSREQUEST_STATE_ERROR";
        return "MEDUSA_DNSREQUEST_STATE_UNKNOWN";
}

__attribute__ ((constructor)) static void dnsrequest_constructor (void)
{
#if defined(MEDUSA_DNSREQUEST_USE_POOL) && (MEDUSA_DNSREQUEST_USE_POOL == 1)
        g_pool = medusa_pool_create("medusa-dnsrequest", sizeof(struct medusa_dnsrequest), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void dnsrequest_destructor (void)
{
#if defined(MEDUSA_DNSREQUEST_USE_POOL) && (MEDUSA_DNSREQUEST_USE_POOL == 1)
        if (g_pool != NULL) {
                medusa_pool_destroy(g_pool);
        }
#endif
}
