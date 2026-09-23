/*
 * a buffered udpsocket must preserve datagram boundaries exactly, however
 * the read buffer happens to be laid out underneath.
 *
 * the read path reserves space for a whole frame and reads into it in place.
 * with the ring backend that reservation is only linear if the free run at
 * the tail of the allocation is long enough for it - and the head only walks
 * forward when a choke leaves data behind, because emptying the buffer resets
 * it to zero. so a consumer that drains every datagram in its callback never
 * moves the head and never sees a short reservation, while one that lets a
 * datagram sit queued eventually does, and used to get the tail of every
 * large datagram silently dropped.
 *
 * both drain policies are exercised here: DRAIN reads each datagram as it
 * arrives, QUEUE keeps one behind so the head cycles through the allocation.
 * every datagram is checked for length and content, and the buffered read
 * event's accounting is checked against what is actually still queued.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

enum {
        POLICY_DRAIN,
        POLICY_QUEUE
};

static const char * g_policies[] = {
        "drain",
        "queue"
};

/* enough datagrams for the ring head to cycle through the read buffer
 * several times over. */
#define NPACKETS        5000

struct context {
        int policy;
        int fd;
        int nsent;
        int nread;
        int queued;
        int failed;
};

static int packet_length (int i)
{
        return 1000 + (i % 400);
}

static unsigned char packet_byte (int i, int offset)
{
        return (unsigned char) ((i * 31 + offset * 7) & 0xff);
}

static int packet_send (struct context *context)
{
        int i;
        int rc;
        int length;
        unsigned char buffer[1400];

        i      = context->nsent;
        length = packet_length(i);
        for (rc = 0; rc < length; rc++) {
                buffer[rc] = packet_byte(i, rc);
        }
        rc = send(context->fd, buffer, length, 0);
        if (rc != length) {
                fprintf(stderr, "  send failed, rc: %d, length: %d\n", rc, length);
                return -1;
        }
        context->nsent += 1;
        return 0;
}

static int packet_read (struct medusa_udpsocket *udpsocket, struct context *context)
{
        int i;
        int offset;
        int length;
        int64_t rc;
        unsigned char buffer[4096];

        i      = context->nread;
        length = packet_length(i);

        rc = medusa_udpsocket_read(udpsocket, buffer, sizeof(buffer));
        if (rc < 0) {
                fprintf(stderr, "  packet %d: medusa_udpsocket_read failed, rc: %d\n", i, (int) rc);
                return -1;
        }
        if (rc != length) {
                fprintf(stderr, "  packet %d: length is %d, expected %d\n", i, (int) rc, length);
                return -1;
        }
        for (offset = 0; offset < length; offset++) {
                if (buffer[offset] != packet_byte(i, offset)) {
                        fprintf(stderr, "  packet %d: byte %d is 0x%02x, expected 0x%02x\n",
                                i, offset, buffer[offset], packet_byte(i, offset));
                        return -1;
                }
        }

        context->nread  += 1;
        context->queued -= 1;
        return 0;
}

/* the event reports the payload still queued, with the frame headers
 * excluded. work out what that should be from the datagrams the test knows
 * it has not read back yet. */
static int packet_check_event (struct context *context, struct medusa_udpsocket_event_buffered_read *event)
{
        int i;
        int64_t remaining;

        if (event->length != packet_length(context->nread + context->queued - 1)) {
                fprintf(stderr, "  packet %d: event length is %d, expected %d\n",
                        context->nread + context->queued - 1, (int) event->length,
                        packet_length(context->nread + context->queued - 1));
                return -1;
        }

        remaining = 0;
        for (i = context->nread; i < context->nread + context->queued; i++) {
                remaining += packet_length(i);
        }
        if (event->remaining != remaining) {
                fprintf(stderr, "  packet %d: event remaining is %d, expected %d\n",
                        context->nread + context->queued - 1, (int) event->remaining, (int) remaining);
                return -1;
        }
        return 0;
}

static int udpsocket_onevent (struct medusa_udpsocket *udpsocket, unsigned int events, void *ctx, void *param)
{
        struct context *context = ctx;

        if (events & MEDUSA_UDPSOCKET_EVENT_ERROR) {
                fprintf(stderr, "  error: %d, %s\n", medusa_udpsocket_get_error(udpsocket), strerror(medusa_udpsocket_get_error(udpsocket)));
                context->failed = 1;
                return medusa_monitor_break(medusa_udpsocket_get_monitor(udpsocket));
        }
        if (!(events & MEDUSA_UDPSOCKET_EVENT_BUFFERED_READ)) {
                return 0;
        }

        context->queued += 1;

        if (packet_check_event(context, param) != 0) {
                goto bail;
        }

        if (context->nsent < NPACKETS) {
                /* under POLICY_QUEUE one datagram is always left behind, so
                 * every choke is a partial one and the ring head keeps
                 * advancing. under POLICY_DRAIN the buffer empties on each
                 * read and the head stays where it is. */
                if (context->queued >= ((context->policy == POLICY_QUEUE) ? 2 : 1)) {
                        if (packet_read(udpsocket, context) != 0) {
                                goto bail;
                        }
                }
                if (packet_send(context) != 0) {
                        goto bail;
                }
                return 0;
        }

        while (context->queued > 0) {
                if (packet_read(udpsocket, context) != 0) {
                        goto bail;
                }
        }
        return medusa_monitor_break(medusa_udpsocket_get_monitor(udpsocket));

bail:   context->failed = 1;
        return medusa_monitor_break(medusa_udpsocket_get_monitor(udpsocket));
}

static int test_policy (unsigned int poll, int policy)
{
        int rc;
        int port;

        struct context context;
        struct sockaddr_in sockaddr;

        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;
        struct medusa_udpsocket *udpsocket;
        struct medusa_udpsocket_bind_options udpsocket_bind_options;

        memset(&context, 0, sizeof(context));
        context.policy = policy;
        context.fd     = -1;

        monitor   = NULL;
        udpsocket = NULL;

        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;

        monitor = medusa_monitor_create_with_options(&options);
        if (monitor == NULL) {
                fprintf(stderr, "  medusa_monitor_create_with_options failed\n");
                goto bail;
        }

        for (port = 22345; port < 65535; port++) {
                rc = medusa_udpsocket_bind_options_default(&udpsocket_bind_options);
                if (rc < 0) {
                        fprintf(stderr, "  medusa_udpsocket_bind_options_default failed\n");
                        goto bail;
                }
                udpsocket_bind_options.monitor     = monitor;
                udpsocket_bind_options.onevent     = udpsocket_onevent;
                udpsocket_bind_options.context     = &context;
                udpsocket_bind_options.protocol    = MEDUSA_UDPSOCKET_PROTOCOL_IPV4;
                udpsocket_bind_options.address     = "127.0.0.1";
                udpsocket_bind_options.port        = port;
                udpsocket_bind_options.reuseaddr   = 1;
                udpsocket_bind_options.nonblocking = 1;
                udpsocket_bind_options.buffered    = 1;
                udpsocket_bind_options.enabled     = 1;

                udpsocket = medusa_udpsocket_bind_with_options(&udpsocket_bind_options);
                if (MEDUSA_IS_ERR_OR_NULL(udpsocket)) {
                        fprintf(stderr, "  medusa_udpsocket_bind_with_options failed\n");
                        goto bail;
                }
                if (medusa_udpsocket_get_state(udpsocket) == MEDUSA_UDPSOCKET_STATE_DISCONNECTED) {
                        medusa_udpsocket_destroy(udpsocket);
                        udpsocket = NULL;
                } else {
                        break;
                }
        }
        if (udpsocket == NULL) {
                fprintf(stderr, "  medusa_udpsocket_bind failed\n");
                goto bail;
        }

        context.fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (context.fd < 0) {
                fprintf(stderr, "  socket failed\n");
                goto bail;
        }
        memset(&sockaddr, 0, sizeof(sockaddr));
        sockaddr.sin_family = AF_INET;
        sockaddr.sin_port   = htons(port);
        rc = inet_pton(AF_INET, "127.0.0.1", &sockaddr.sin_addr);
        if (rc != 1) {
                fprintf(stderr, "  inet_pton failed\n");
                goto bail;
        }
        rc = connect(context.fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr));
        if (rc != 0) {
                fprintf(stderr, "  connect failed\n");
                goto bail;
        }

        /* prime the exchange, every later datagram is sent from the read
         * callback so only one is ever in flight. */
        if (packet_send(&context) != 0) {
                goto bail;
        }

        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                fprintf(stderr, "  medusa_monitor_run failed, rc: %d\n", rc);
                goto bail;
        }
        if (context.failed != 0) {
                goto bail;
        }
        if (context.nread != NPACKETS) {
                fprintf(stderr, "  read %d datagrams, expected %d\n", context.nread, NPACKETS);
                goto bail;
        }

        close(context.fd);
        medusa_monitor_destroy(monitor);
        return 0;
bail:   if (context.fd >= 0) {
                close(context.fd);
        }
        if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

static int test_poll (unsigned int poll)
{
        int rc;
        unsigned int i;
        for (i = 0; i < sizeof(g_policies) / sizeof(g_policies[0]); i++) {
                fprintf(stderr, "  policy: %s\n", g_policies[i]);
                rc = test_policy(poll, i);
                if (rc != 0) {
                        return -1;
                }
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
                alarm(60);

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
