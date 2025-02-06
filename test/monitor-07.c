
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/monitor.h"

#include "medusa/queue.h"
#include "medusa/monitor-private.h"
#include "medusa/subject-struct.h"

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

static int test_poll (unsigned int poll)
{
        int rc;

        struct medusa_subject *subject;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        monitor = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                fprintf(stderr, "medusa_monitor_create failed\n");
                goto bail;
        }

        fprintf(stderr, "running monitor\n");
        rc = medusa_monitor_run_once(monitor);
        if (rc < 0) {
                fprintf(stderr, "medusa_monitor_run_once failed\n");
                goto bail;
        }

        medusa_monitor_lock(monitor);
        for (subject = medusa_monitor_get_first_subject_unlocked(monitor); subject; subject = medusa_monitor_get_next_subject_unlocked(subject)) {
                fprintf(stdout, "subject: %p, type: %d, active: %d\n", subject, medusa_subject_get_type(subject), medusa_subject_is_active(subject));
                switch (medusa_subject_get_type(subject)) {
                        case MEDUSA_SUBJECT_TYPE_IO :
                                break;
                        case MEDUSA_SUBJECT_TYPE_TIMER:
                                break;
                        case MEDUSA_SUBJECT_TYPE_SIGNAL:
                                break;
                        case MEDUSA_SUBJECT_TYPE_CONDITION:
                                break;
                        case MEDUSA_SUBJECT_TYPE_TCPSOCKET:
                                break;
                        case MEDUSA_SUBJECT_TYPE_UDPSOCKET:
                                break;
                        case MEDUSA_SUBJECT_TYPE_EXEC:
                                break;
                        case MEDUSA_SUBJECT_TYPE_HTTPREQUEST:
                                break;
                        case MEDUSA_SUBJECT_TYPE_DNSREQUEST:
                                break;
                        case MEDUSA_SUBJECT_TYPE_DNSRESOLVER:
                                break;
                        case MEDUSA_SUBJECT_TYPE_DNSRESOLVER_LOOKUP:
                                break;
                        case MEDUSA_SUBJECT_TYPE_WEBSOCKETCLIENT:
                                break;
                        case MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER:
                                break;
                        case MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER_CLIENT:
                                break;
                        case MEDUSA_SUBJECT_TYPE_HTTPSERVER:
                                break;
                        case MEDUSA_SUBJECT_TYPE_HTTPSERVER_CLIENT:
                                break;
                }

        }
        medusa_monitor_unlock(monitor);

        fprintf(stderr, "finish\n");

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

static void alarm_handler (int sig)
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
        signal(SIGALRM, alarm_handler);

        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                alarm(5);
                fprintf(stderr, "testing poll: %d\n", g_polls[i]);
                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        fprintf(stderr, "poll: %d test failed\n", g_polls[i]);
                        return -1;
                }
        }

        return 0;
}
