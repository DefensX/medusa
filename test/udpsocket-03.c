
/*
 * medusa_udpsocket_connect_with_options() must never deliver an event from
 * inside the call itself.
 *
 * it used to resolve and connect inline, so a synchronous failure - getaddrinfo,
 * socket, bind - fired the error event before the caller ever received the
 * udpsocket pointer. a caller that tears down its context from the error
 * callback then got a valid looking pointer back for an object it had already
 * given up on, and never retried. the resolve and connect work is deferred to
 * the monitor now, so every outcome is delivered from the loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/udpsocket.h"
#include "medusa/monitor.h"

static const unsigned int g_polls[] = {
        MEDUSA_MONITOR_POLL_DEFAULT,
#if defined(__LINUX__)
        MEDUSA_MONITOR_POLL_EPOLL,
#endif
#if defined(__APPLE__)
        MEDUSA_MONITOR_POLL_KQUEUE,
#endif
        MEDUSA_MONITOR_POLL_POLL,
        MEDUSA_MONITOR_POLL_SELECT,
#if defined(__WINDOWS__)
        MEDUSA_MONITOR_POLL_WSAPOLL,
#endif
};

/* set while medusa_udpsocket_connect_with_options() is on the stack */
static int g_inconnect;
/* set once the caller has destroyed the udpsocket */
static int g_destroyed;

static int g_nevents_inconnect;
static int g_nerrors_inconnect;
static int g_nevents_afterdestroy;
static int g_nerrors;
static int g_error;

static void counters_reset (void)
{
        g_inconnect             = 0;
        g_destroyed             = 0;
        g_nevents_inconnect     = 0;
        g_nerrors_inconnect     = 0;
        g_nevents_afterdestroy  = 0;
        g_nerrors               = 0;
        g_error                 = 0;
}

static int udpsocket_onevent (struct medusa_udpsocket *udpsocket, unsigned int events, void *context, void *param)
{
        (void) context;
        (void) param;

        fprintf(stderr, "    events: 0x%08x, %s\n", events, medusa_udpsocket_event_string(events));

        if (g_inconnect != 0) {
                g_nevents_inconnect += 1;
                if (events & MEDUSA_UDPSOCKET_EVENT_ERROR) {
                        g_nerrors_inconnect += 1;
                }
        }
        if (g_destroyed != 0 &&
            (events & MEDUSA_UDPSOCKET_EVENT_DESTROY) == 0) {
                g_nevents_afterdestroy += 1;
        }
        if (events & MEDUSA_UDPSOCKET_EVENT_ERROR) {
                g_nerrors += 1;
                g_error = medusa_udpsocket_get_error(udpsocket);
                fprintf(stderr, "      state: %d, error: %d\n", medusa_udpsocket_get_state(udpsocket), g_error);
        }
        return 0;
}

static int monitor_pump (struct medusa_monitor *monitor, double seconds, int untilerror)
{
        int rc;
        int i;
        int n;
        n = (int) (seconds / 0.05);
        for (i = 0; i < n; i++) {
                rc = medusa_monitor_run_timeout(monitor, 0.05);
                if (rc < 0) {
                        fprintf(stderr, "medusa_monitor_run_timeout failed, rc: %d\n", rc);
                        return rc;
                }
                if (rc == 0) {
                        break;
                }
                if (untilerror != 0 &&
                    g_nerrors > 0) {
                        break;
                }
        }
        return 0;
}

static struct medusa_monitor * monitor_create (unsigned int poll)
{
        struct medusa_monitor_init_options monitor_init_options;
        medusa_monitor_init_options_default(&monitor_init_options);
        monitor_init_options.poll.type = poll;
        return medusa_monitor_create_with_options(&monitor_init_options);
}

static int connect_options_default (struct medusa_udpsocket_connect_options *options, struct medusa_monitor *monitor)
{
        int rc;
        rc = medusa_udpsocket_connect_options_default(options);
        if (rc < 0) {
                fprintf(stderr, "medusa_udpsocket_connect_options_default failed, rc: %d\n", rc);
                return rc;
        }
        options->monitor        = monitor;
        options->onevent        = udpsocket_onevent;
        options->context        = NULL;
        options->nonblocking    = 1;
        options->enabled        = 1;
        return 0;
}

/*
 * connect() rewrites loopback and any to a literal address before it decides
 * whether the address needs resolving, and getaddrinfo() does not know either
 * name. the deferred work has to see the rewritten address, not the one the
 * caller passed in.
 */
static int test_rewritten_address (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_udpsocket *udpsocket;
        struct medusa_udpsocket_connect_options udpsocket_connect_options;

        monitor = NULL;
        counters_reset();

        fprintf(stderr, "  rewritten address\n");

        monitor = monitor_create(poll);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "medusa_monitor_create_with_options failed\n");
                goto bail;
        }

        rc = connect_options_default(&udpsocket_connect_options, monitor);
        if (rc < 0) {
                goto bail;
        }
        udpsocket_connect_options.protocol      = MEDUSA_UDPSOCKET_PROTOCOL_ANY;
        udpsocket_connect_options.address       = "loopback";
        udpsocket_connect_options.port          = 12345;

        udpsocket = medusa_udpsocket_connect_with_options(&udpsocket_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(udpsocket)) {
                fprintf(stderr, "medusa_udpsocket_connect_with_options failed, error: %d\n", MEDUSA_PTR_ERR(udpsocket));
                goto bail;
        }

        rc = monitor_pump(monitor, 2.0, 1);
        if (rc < 0) {
                goto bail;
        }

        /*
         * a resolver error means the deferred work was handed the name the
         * caller passed in rather than the rewritten address.
         */
        if (g_error == ENOENT) {
                fprintf(stderr, "loopback was not rewritten before it was resolved\n");
                goto bail;
        }

        medusa_udpsocket_destroy(udpsocket);
        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

/*
 * a source address that can not be bound makes connect_resolved() fail without
 * touching the resolver or the network, so this case is deterministic offline.
 */
static int test_connect_resolved_failure (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_udpsocket *udpsocket;
        struct medusa_udpsocket_connect_options udpsocket_connect_options;

        monitor = NULL;
        counters_reset();

        fprintf(stderr, "  connect_resolved failure\n");

        monitor = monitor_create(poll);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "medusa_monitor_create_with_options failed\n");
                goto bail;
        }

        rc = connect_options_default(&udpsocket_connect_options, monitor);
        if (rc < 0) {
                goto bail;
        }
        udpsocket_connect_options.protocol      = MEDUSA_UDPSOCKET_PROTOCOL_IPV4;
        udpsocket_connect_options.address       = "127.0.0.1";
        udpsocket_connect_options.port          = 12345;
        udpsocket_connect_options.sprotocol     = MEDUSA_UDPSOCKET_PROTOCOL_IPV4;
        udpsocket_connect_options.saddress      = "1.2.3.4";

        g_inconnect = 1;
        udpsocket = medusa_udpsocket_connect_with_options(&udpsocket_connect_options);
        g_inconnect = 0;

        if (MEDUSA_IS_ERR_OR_NULL(udpsocket)) {
                fprintf(stderr, "medusa_udpsocket_connect_with_options failed, error: %d\n", MEDUSA_PTR_ERR(udpsocket));
                goto bail;
        }
        if (g_nerrors_inconnect != 0) {
                fprintf(stderr, "error event delivered from inside connect(), count: %d\n", g_nerrors_inconnect);
                goto bail;
        }
        if (g_nevents_inconnect != 0) {
                fprintf(stderr, "warning: %d event(s) delivered from inside connect()\n", g_nevents_inconnect);
        }

        rc = monitor_pump(monitor, 2.0, 1);
        if (rc < 0) {
                goto bail;
        }

        if (g_nerrors == 0) {
                fprintf(stderr, "expected an error event from the monitor\n");
                goto bail;
        }
        if (g_error <= 0) {
                fprintf(stderr, "error must be a positive errno, got: %d\n", g_error);
                goto bail;
        }
        if (medusa_udpsocket_get_state(udpsocket) != MEDUSA_UDPSOCKET_STATE_ERROR) {
                fprintf(stderr, "expected state error, got: %d\n", medusa_udpsocket_get_state(udpsocket));
                goto bail;
        }

        medusa_udpsocket_destroy(udpsocket);
        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

/*
 * .invalid is reserved by rfc 2606 and must not resolve. a resolver that
 * answers anyway only costs us the error assertion, the invariant that no
 * event may be delivered from inside connect() still holds.
 */
static int test_unresolvable_address (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_udpsocket *udpsocket;
        struct medusa_udpsocket_connect_options udpsocket_connect_options;

        monitor = NULL;
        counters_reset();

        fprintf(stderr, "  unresolvable address\n");

        monitor = monitor_create(poll);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "medusa_monitor_create_with_options failed\n");
                goto bail;
        }

        rc = connect_options_default(&udpsocket_connect_options, monitor);
        if (rc < 0) {
                goto bail;
        }
        udpsocket_connect_options.protocol      = MEDUSA_UDPSOCKET_PROTOCOL_ANY;
        udpsocket_connect_options.address       = "medusa-test-nonexistent-host.invalid";
        udpsocket_connect_options.port          = 80;

        g_inconnect = 1;
        udpsocket = medusa_udpsocket_connect_with_options(&udpsocket_connect_options);
        g_inconnect = 0;

        if (MEDUSA_IS_ERR_OR_NULL(udpsocket)) {
                fprintf(stderr, "medusa_udpsocket_connect_with_options failed, error: %d\n", MEDUSA_PTR_ERR(udpsocket));
                goto bail;
        }
        if (g_nerrors_inconnect != 0) {
                fprintf(stderr, "error event delivered from inside connect(), count: %d\n", g_nerrors_inconnect);
                goto bail;
        }
        if (g_nevents_inconnect != 0) {
                fprintf(stderr, "warning: %d event(s) delivered from inside connect()\n", g_nevents_inconnect);
        }

        rc = monitor_pump(monitor, 5.0, 1);
        if (rc < 0) {
                goto bail;
        }

        if (g_nerrors == 0) {
                fprintf(stderr, "warning: address resolved, skipping error assertion\n");
        } else if (g_error <= 0) {
                fprintf(stderr, "error must be a positive errno, got: %d\n", g_error);
                goto bail;
        }

        medusa_udpsocket_destroy(udpsocket);
        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

/*
 * the caller may destroy the udpsocket between connect() returning and the
 * deferred work running. nothing may be delivered afterwards, and the deferred
 * work must not touch the dead object.
 */
static int test_destroy_before_deferred (unsigned int poll)
{
        int rc;

        struct medusa_monitor *monitor;
        struct medusa_udpsocket *udpsocket;
        struct medusa_udpsocket_connect_options udpsocket_connect_options;

        monitor = NULL;
        counters_reset();

        fprintf(stderr, "  destroy before deferred work\n");

        monitor = monitor_create(poll);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "medusa_monitor_create_with_options failed\n");
                goto bail;
        }

        rc = connect_options_default(&udpsocket_connect_options, monitor);
        if (rc < 0) {
                goto bail;
        }
        udpsocket_connect_options.protocol      = MEDUSA_UDPSOCKET_PROTOCOL_IPV4;
        udpsocket_connect_options.address       = "127.0.0.1";
        udpsocket_connect_options.port          = 12345;
        udpsocket_connect_options.sprotocol     = MEDUSA_UDPSOCKET_PROTOCOL_IPV4;
        udpsocket_connect_options.saddress      = "1.2.3.4";

        udpsocket = medusa_udpsocket_connect_with_options(&udpsocket_connect_options);
        if (MEDUSA_IS_ERR_OR_NULL(udpsocket)) {
                fprintf(stderr, "medusa_udpsocket_connect_with_options failed, error: %d\n", MEDUSA_PTR_ERR(udpsocket));
                goto bail;
        }

        g_destroyed = 1;
        medusa_udpsocket_destroy(udpsocket);

        rc = monitor_pump(monitor, 1.0, 0);
        if (rc < 0) {
                goto bail;
        }

        if (g_nevents_afterdestroy != 0) {
                fprintf(stderr, "%d event(s) delivered after destroy()\n", g_nevents_afterdestroy);
                goto bail;
        }

        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

static int test_poll (unsigned int poll)
{
        int rc;
        rc = test_rewritten_address(poll);
        if (rc < 0) {
                return rc;
        }
        rc = test_connect_resolved_failure(poll);
        if (rc < 0) {
                return rc;
        }
        rc = test_unresolvable_address(poll);
        if (rc < 0) {
                return rc;
        }
        rc = test_destroy_before_deferred(poll);
        if (rc < 0) {
                return rc;
        }
        return 0;
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
                alarm(30);

                fprintf(stderr, "testing poll: %d\n", g_polls[i]);
                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        fprintf(stderr, "  failed\n");
                        return -1;
                }
                fprintf(stderr, "success\n");
        }
        return 0;
}
