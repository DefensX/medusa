
/*
 * windows poll backend checks. every scenario runs against the iocp backend,
 * the iocp backend with every socket polled the layered way
 * (MEDUSA_POLL_IOCP_SLOW), and wsapoll for reference:
 *
 *   1. loopback tcp through ios: accept, connect, echo, peer half close
 *   2. a peer reset is reported as hup or err
 *   3. a refused non blocking connect reports out or err, SO_ERROR is set
 *   4. changing the interest of a socket with a pending poll
 *   5. an io destroyed by another thread while its poll is pending
 *   6. a socket closed under its io is reported once, not spun on
 *   7. a hundred sockets, more than one afd group
 *   8. a destroyed io gets nothing after its handle value is reused
 *
 * and MEDUSA_POLL_IOCP_DISABLE makes the default fall back to wsapoll.
 *
 * a watchdog names the step that takes too long and exits with 3.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(__WINDOWS__)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <pthread.h>

#include "medusa/error.h"
#include "medusa/io.h"
#include "medusa/monitor.h"

#define STEP_TIMEOUT    15.0
#define MANY            100

enum {
        MODE_IOCP,
        MODE_IOCP_SLOW,
        MODE_WSAPOLL
};

static const char * volatile g_step = "start";
static volatile double g_deadline;

static double now (void)
{
        LARGE_INTEGER frequency;
        LARGE_INTEGER counter;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&counter);
        return (double) counter.QuadPart / (double) frequency.QuadPart;
}

static void step (const char *name)
{
        g_step     = name;
        g_deadline = now() + STEP_TIMEOUT;
        fprintf(stderr, "    %s\n", name);
}

static void * watchdog_routine (void *arg)
{
        (void) arg;
        while (1) {
                Sleep(100);
                if (g_deadline > 0 && now() > g_deadline) {
                        fprintf(stderr, "step: %s timed out\n", g_step);
                        fflush(stderr);
                        _exit(3);
                }
        }
        return NULL;
}

static const char * mode_string (int mode)
{
        switch (mode) {
                case MODE_IOCP:         return "iocp";
                case MODE_IOCP_SLOW:    return "iocp, every socket polled with wsapoll";
                case MODE_WSAPOLL:      return "wsapoll";
        }
        return "unknown";
}

static struct medusa_monitor * monitor_create (int mode)
{
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;
        medusa_monitor_init_options_default(&options);
        options.poll.type   = (mode == MODE_WSAPOLL) ? MEDUSA_MONITOR_POLL_WSAPOLL : MEDUSA_MONITOR_POLL_IOCP;
        options.signal.type = MEDUSA_MONITOR_SIGNAL_NULL;
        if (mode == MODE_IOCP_SLOW) {
                _putenv("MEDUSA_POLL_IOCP_SLOW=1");
        }
        monitor = medusa_monitor_create_with_options(&options);
        if (mode == MODE_IOCP_SLOW) {
                _putenv("MEDUSA_POLL_IOCP_SLOW=");
        }
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "    medusa_monitor_create failed\n");
                return NULL;
        }
        return monitor;
}

static int run_until (struct medusa_monitor *monitor, volatile int *flag, double timeout)
{
        int rc;
        double end;
        end = now() + timeout;
        while (*flag == 0) {
                if (now() > end) {
                        fprintf(stderr, "    timed out\n");
                        return -1;
                }
                rc = medusa_monitor_run_timeout(monitor, 0.05);
                if (rc < 0) {
                        fprintf(stderr, "    medusa_monitor_run_timeout failed, rc: %d\n", rc);
                        return -1;
                }
        }
        return 0;
}

static int run_for (struct medusa_monitor *monitor, double seconds)
{
        int rc;
        double end;
        end = now() + seconds;
        while (now() < end) {
                rc = medusa_monitor_run_timeout(monitor, 0.05);
                if (rc < 0) {
                        fprintf(stderr, "    medusa_monitor_run_timeout failed, rc: %d\n", rc);
                        return -1;
                }
        }
        return 0;
}

static int nonblock (SOCKET socket)
{
        u_long nb = 1;
        return ioctlsocket(socket, FIONBIO, &nb);
}

static SOCKET tcp_listener (struct sockaddr_in *address)
{
        int length;
        SOCKET listener;
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) {
                return INVALID_SOCKET;
        }
        memset(address, 0, sizeof(*address));
        address->sin_family      = AF_INET;
        address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        length = sizeof(*address);
        if (bind(listener, (struct sockaddr *) address, sizeof(*address)) != 0 ||
            listen(listener, 16) != 0 ||
            getsockname(listener, (struct sockaddr *) address, &length) != 0) {
                closesocket(listener);
                return INVALID_SOCKET;
        }
        return listener;
}

/* an established loopback connection, both ends non blocking */
static int tcp_pair (SOCKET *client, SOCKET *server)
{
        SOCKET listener;
        struct sockaddr_in address;
        *client = INVALID_SOCKET;
        *server = INVALID_SOCKET;
        listener = tcp_listener(&address);
        if (listener == INVALID_SOCKET) {
                return -1;
        }
        *client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (*client == INVALID_SOCKET ||
            connect(*client, (struct sockaddr *) &address, sizeof(address)) != 0) {
                goto bail;
        }
        *server = accept(listener, NULL, NULL);
        if (*server == INVALID_SOCKET) {
                goto bail;
        }
        closesocket(listener);
        nonblock(*client);
        nonblock(*server);
        return 0;
bail:   closesocket(listener);
        if (*client != INVALID_SOCKET) {
                closesocket(*client);
        }
        *client = INVALID_SOCKET;
        return -1;
}

static struct medusa_io * io_create (struct medusa_monitor *monitor, SOCKET socket, unsigned int events, int clodestroy, int (*onevent) (struct medusa_io *io, unsigned int events, void *context, void *param), void *context)
{
        struct medusa_io *io;
        io = medusa_io_create(monitor, (int) socket, onevent, context);
        if (MEDUSA_IS_ERR_OR_NULL(io)) {
                fprintf(stderr, "    medusa_io_create failed\n");
                return NULL;
        }
        if (medusa_io_set_events(io, events) < 0 ||
            medusa_io_set_clodestroy(io, clodestroy) < 0 ||
            medusa_io_set_enabled(io, 1) < 0) {
                fprintf(stderr, "    can not setup io\n");
                medusa_io_destroy(io);
                return NULL;
        }
        return io;
}

/* 1. echo */

struct echo {
        struct medusa_monitor *monitor;
        struct medusa_io *server;
        volatile int accepted;
        volatile int connected;
        volatile int echoed;
        volatile int received;
        volatile int closed;
        int failed;
};

static int echo_server_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        char buffer[64];
        struct echo *echo = context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        if (events & (MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_HUP | MEDUSA_IO_EVENT_ERR)) {
                rc = recv((SOCKET) medusa_io_get_fd(io), buffer, sizeof(buffer), 0);
                if (rc > 0) {
                        send((SOCKET) medusa_io_get_fd(io), buffer, rc, 0);
                        echo->echoed = 1;
                } else if (rc == 0) {
                        echo->closed = 1;
                        medusa_io_set_enabled(io, 0);
                } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
                        echo->failed = 1;
                        echo->closed = 1;
                        medusa_io_set_enabled(io, 0);
                }
        }
        return 0;
}

static int echo_listener_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        SOCKET socket;
        struct echo *echo = context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_IN) {
                socket = accept((SOCKET) medusa_io_get_fd(io), NULL, NULL);
                if (socket == INVALID_SOCKET) {
                        return 0;
                }
                nonblock(socket);
                echo->server = io_create(echo->monitor, socket, MEDUSA_IO_EVENT_IN, 1, echo_server_onevent, echo);
                if (echo->server == NULL) {
                        closesocket(socket);
                        echo->failed = 1;
                }
                echo->accepted = 1;
                medusa_io_set_enabled(io, 0);
        }
        return 0;
}

static int echo_client_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        int error;
        int length;
        char buffer[64];
        SOCKET socket;
        struct echo *echo = context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        socket = (SOCKET) medusa_io_get_fd(io);
        if ((events & MEDUSA_IO_EVENT_OUT) && echo->connected == 0) {
                error  = 0;
                length = sizeof(error);
                getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *) &error, &length);
                if (error != 0) {
                        fprintf(stderr, "    connect failed, error: %d\n", error);
                        echo->failed = 1;
                        return 0;
                }
                echo->connected = 1;
                send(socket, "ping", 4, 0);
                medusa_io_set_events(io, MEDUSA_IO_EVENT_IN);
        }
        if (events & MEDUSA_IO_EVENT_IN) {
                rc = recv(socket, buffer, sizeof(buffer), 0);
                if (rc == 4 && memcmp(buffer, "ping", 4) == 0) {
                        echo->received = 1;
                }
        }
        return 0;
}

static int test_echo (int mode)
{
        int rc;
        SOCKET listener;
        SOCKET client;
        struct sockaddr_in address;
        struct medusa_io *lio;
        struct medusa_io *cio;
        struct echo echo;
        struct medusa_monitor *monitor;

        step("echo");
        rc = -1;
        lio = NULL;
        cio = NULL;
        memset(&echo, 0, sizeof(echo));
        monitor = monitor_create(mode);
        if (monitor == NULL) {
                return -1;
        }
        echo.monitor = monitor;
        listener = tcp_listener(&address);
        client   = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET || client == INVALID_SOCKET) {
                goto out;
        }
        nonblock(listener);
        nonblock(client);
        lio = io_create(monitor, listener, MEDUSA_IO_EVENT_IN, 1, echo_listener_onevent, &echo);
        if (lio == NULL) {
                closesocket(client);
                goto out;
        }
        if (connect(client, (struct sockaddr *) &address, sizeof(address)) == 0 ||
            WSAGetLastError() != WSAEWOULDBLOCK) {
                fprintf(stderr, "    connect did not go pending, error: %d\n", WSAGetLastError());
                closesocket(client);
                goto out;
        }
        cio = io_create(monitor, client, MEDUSA_IO_EVENT_OUT, 1, echo_client_onevent, &echo);
        if (cio == NULL) {
                closesocket(client);
                goto out;
        }
        if (run_until(monitor, &echo.accepted, 5.0) != 0 ||
            run_until(monitor, &echo.connected, 5.0) != 0 ||
            run_until(monitor, &echo.received, 5.0) != 0) {
                goto out;
        }
        step("echo, peer half close");
        shutdown(client, SD_SEND);
        if (run_until(monitor, &echo.closed, 5.0) != 0) {
                goto out;
        }
        if (echo.failed) {
                fprintf(stderr, "    echo failed\n");
                goto out;
        }
        rc = 0;
out:    if (listener != INVALID_SOCKET && lio == NULL) {
                closesocket(listener);
        }
        medusa_monitor_destroy(monitor);
        return rc;
}

/* 2. reset, 4. mod, 6. local close, 7. many, 8. reuse share this one */

struct counter {
        volatile int in;
        volatile int out;
        volatile int hup;
        volatile int err;
        volatile int nval;
        volatile int any;
        volatile int destroyed;
        volatile int reset;
        volatile int *total;
        unsigned int events;
};

static int counter_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        char buffer[64];
        struct counter *counter = context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                counter->destroyed = 1;
                return 0;
        }
        if (counter->any == 0 && counter->total != NULL) {
                *counter->total += 1;
        }
        counter->any    += 1;
        counter->events |= events;
        if (events & MEDUSA_IO_EVENT_IN) {
                counter->in += 1;
                rc = recv((SOCKET) medusa_io_get_fd(io), buffer, sizeof(buffer), 0);
                if (rc < 0 && WSAGetLastError() == WSAECONNRESET) {
                        counter->reset = 1;
                }
                if (rc == 0) {
                        medusa_io_set_enabled(io, 0);
                }
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                counter->out += 1;
        }
        if (events & MEDUSA_IO_EVENT_HUP) {
                counter->hup += 1;
        }
        if (events & MEDUSA_IO_EVENT_ERR) {
                counter->err += 1;
        }
        if (events & MEDUSA_IO_EVENT_NVAL) {
                counter->nval += 1;
        }
        return 0;
}

static int test_reset (int mode)
{
        int rc;
        volatile int seen;
        SOCKET client;
        SOCKET server;
        struct linger linger;
        struct counter counter;
        struct medusa_io *io;
        struct medusa_monitor *monitor;

        step("reset");
        rc = -1;
        memset(&counter, 0, sizeof(counter));
        monitor = monitor_create(mode);
        if (monitor == NULL) {
                return -1;
        }
        if (tcp_pair(&client, &server) != 0) {
                goto out;
        }
        io = io_create(monitor, server, MEDUSA_IO_EVENT_IN, 1, counter_onevent, &counter);
        if (io == NULL) {
                closesocket(server);
                closesocket(client);
                goto out;
        }
        if (run_for(monitor, 0.2) != 0) {
                closesocket(client);
                goto out;
        }
        linger.l_onoff  = 1;
        linger.l_linger = 0;
        setsockopt(client, SOL_SOCKET, SO_LINGER, (const char *) &linger, sizeof(linger));
        closesocket(client);
        seen = 0;
        while (seen == 0) {
                if (run_for(monitor, 0.05) != 0) {
                        goto out;
                }
                seen = (counter.hup || counter.err || counter.reset);
                if (now() > g_deadline - 5.0) {
                        break;
                }
        }
        if (!seen) {
                fprintf(stderr, "    reset is not reported, events: 0x%08x\n", counter.events);
                goto out;
        }
        rc = 0;
out:    medusa_monitor_destroy(monitor);
        return rc;
}

/* 3. connect fail */

struct refused {
        volatile int done;
        int error;
        unsigned int events;
};

static int refused_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int length;
        struct refused *refused = context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        refused->events |= events;
        length = sizeof(refused->error);
        getsockopt((SOCKET) medusa_io_get_fd(io), SOL_SOCKET, SO_ERROR, (char *) &refused->error, &length);
        refused->done = 1;
        medusa_io_set_enabled(io, 0);
        return 0;
}

static int test_connect_fail (int mode)
{
        int rc;
        SOCKET closed;
        SOCKET client;
        struct sockaddr_in address;
        struct refused refused;
        struct medusa_io *io;
        struct medusa_monitor *monitor;

        step("connect fail");
        rc = -1;
        memset(&refused, 0, sizeof(refused));
        monitor = monitor_create(mode);
        if (monitor == NULL) {
                return -1;
        }
        closed = tcp_listener(&address);
        if (closed == INVALID_SOCKET) {
                goto out;
        }
        closesocket(closed);
        client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET) {
                goto out;
        }
        nonblock(client);
        if (connect(client, (struct sockaddr *) &address, sizeof(address)) == 0 ||
            WSAGetLastError() != WSAEWOULDBLOCK) {
                fprintf(stderr, "    connect did not go pending, error: %d\n", WSAGetLastError());
                closesocket(client);
                goto out;
        }
        io = io_create(monitor, client, MEDUSA_IO_EVENT_OUT, 1, refused_onevent, &refused);
        if (io == NULL) {
                closesocket(client);
                goto out;
        }
        /* windows retries a refused loopback syn, this takes about two seconds */
        if (run_until(monitor, &refused.done, 10.0) != 0) {
                goto out;
        }
        if (!(refused.events & (MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_ERR))) {
                fprintf(stderr, "    refused connect events: 0x%08x\n", refused.events);
                goto out;
        }
        if (refused.error != WSAECONNREFUSED) {
                fprintf(stderr, "    SO_ERROR is: %d, expected: %d\n", refused.error, WSAECONNREFUSED);
                goto out;
        }
        rc = 0;
out:    medusa_monitor_destroy(monitor);
        return rc;
}

/* 4. mod */

static int test_mod (int mode)
{
        int rc;
        int out;
        SOCKET client;
        SOCKET server;
        struct counter counter;
        struct medusa_io *io;
        struct medusa_monitor *monitor;

        step("mod");
        rc = -1;
        memset(&counter, 0, sizeof(counter));
        monitor = monitor_create(mode);
        if (monitor == NULL) {
                return -1;
        }
        if (tcp_pair(&client, &server) != 0) {
                goto out;
        }
        io = io_create(monitor, client, MEDUSA_IO_EVENT_IN, 1, counter_onevent, &counter);
        if (io == NULL) {
                closesocket(client);
                closesocket(server);
                goto out;
        }
        if (run_for(monitor, 0.3) != 0) {
                goto close;
        }
        if (counter.any != 0) {
                fprintf(stderr, "    idle socket reported events: 0x%08x\n", counter.events);
                goto close;
        }
        step("mod, in to out");
        medusa_io_set_events(io, MEDUSA_IO_EVENT_OUT);
        if (run_until(monitor, &counter.out, 2.0) != 0) {
                goto close;
        }
        step("mod, out to in");
        medusa_io_set_events(io, MEDUSA_IO_EVENT_IN);
        out = counter.out;
        if (run_for(monitor, 0.3) != 0) {
                goto close;
        }
        if (counter.out != out) {
                fprintf(stderr, "    out is reported after it is not asked for, out: %d -> %d\n", out, counter.out);
                goto close;
        }
        step("mod, data arrives");
        send(server, "x", 1, 0);
        if (run_until(monitor, &counter.in, 2.0) != 0) {
                goto close;
        }
        rc = 0;
close:  closesocket(server);
out:    medusa_monitor_destroy(monitor);
        return rc;
}

/* 5. destroy pending from another thread */

struct destroyer {
        struct medusa_monitor *monitor;
        struct medusa_io *io;
};

static void * destroyer_routine (void *arg)
{
        struct destroyer *destroyer = arg;
        Sleep(200);
        medusa_io_destroy(destroyer->io);
        Sleep(200);
        medusa_monitor_break(destroyer->monitor);
        return NULL;
}

static int test_destroy_pending (int mode)
{
        int rc;
        double start;
        SOCKET client;
        SOCKET server;
        pthread_t thread;
        struct counter counter;
        struct counter scounter;
        struct destroyer destroyer;
        struct medusa_io *io;
        struct medusa_monitor *monitor;

        step("destroy pending");
        rc = -1;
        memset(&counter, 0, sizeof(counter));
        memset(&scounter, 0, sizeof(scounter));
        monitor = monitor_create(mode);
        if (monitor == NULL) {
                return -1;
        }
        if (tcp_pair(&client, &server) != 0) {
                goto out;
        }
        io = io_create(monitor, client, MEDUSA_IO_EVENT_IN, 1, counter_onevent, &counter);
        if (io == NULL) {
                closesocket(client);
                closesocket(server);
                goto out;
        }
        if (io_create(monitor, server, MEDUSA_IO_EVENT_IN, 1, counter_onevent, &scounter) == NULL) {
                closesocket(server);
                goto out;
        }
        if (run_for(monitor, 0.1) != 0) {
                goto out;
        }
        destroyer.monitor = monitor;
        destroyer.io      = io;
        if (pthread_create(&thread, NULL, destroyer_routine, &destroyer) != 0) {
                goto out;
        }
        start = now();
        do {
                rc = medusa_monitor_run_timeout(monitor, 10.0);
        } while (rc == 1 && now() - start < 5.0);
        pthread_join(thread, NULL);
        if (rc != 0) {
                fprintf(stderr, "    loop did not break, rc: %d\n", rc);
                rc = -1;
                goto out;
        }
        medusa_monitor_continue(monitor);
        if (run_until(monitor, &counter.destroyed, 2.0) != 0) {
                rc = -1;
                goto out;
        }
        /* the server end is still armed, destroy drains it */
        rc = 0;
out:    medusa_monitor_destroy(monitor);
        return rc;
}

/* 6. local close */

static int test_local_close (int mode)
{
        int rc;
        SOCKET client;
        SOCKET server;
        struct counter counter;
        struct medusa_io *io;
        struct medusa_monitor *monitor;

        step("local close");
        rc = -1;
        memset(&counter, 0, sizeof(counter));
        monitor = monitor_create(mode);
        if (monitor == NULL) {
                return -1;
        }
        if (tcp_pair(&client, &server) != 0) {
                goto out;
        }
        io = io_create(monitor, client, MEDUSA_IO_EVENT_IN, 0, counter_onevent, &counter);
        if (io == NULL) {
                closesocket(client);
                closesocket(server);
                goto out;
        }
        if (run_for(monitor, 0.2) != 0) {
                goto close;
        }
        closesocket(client);
        if (run_until(monitor, &counter.nval, 2.0) != 0) {
                fprintf(stderr, "    closed socket is not reported, events: 0x%08x\n", counter.events);
                goto close;
        }
        if (run_for(monitor, 0.3) != 0) {
                goto close;
        }
        if (mode == MODE_IOCP && counter.nval != 1) {
                fprintf(stderr, "    closed socket is reported more than once, nval: %d\n", counter.nval);
                goto close;
        }
        medusa_io_destroy(io);
        rc = 0;
close:  closesocket(server);
out:    medusa_monitor_destroy(monitor);
        return rc;
}

/* 7. many */

static int test_many (int mode)
{
        int i;
        int rc;
        int length;
        volatile int total;
        SOCKET sender;
        SOCKET sockets[MANY];
        struct sockaddr_in addresses[MANY];
        struct counter counters[MANY];
        struct medusa_io *ios[MANY];
        struct medusa_monitor *monitor;

        step("many");
        rc = -1;
        total = 0;
        sender = INVALID_SOCKET;
        memset(counters, 0, sizeof(counters));
        memset(ios, 0, sizeof(ios));
        monitor = monitor_create(mode);
        if (monitor == NULL) {
                return -1;
        }
        for (i = 0; i < MANY; i++) {
                sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (sockets[i] == INVALID_SOCKET) {
                        goto out;
                }
                memset(&addresses[i], 0, sizeof(addresses[i]));
                addresses[i].sin_family      = AF_INET;
                addresses[i].sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                length = sizeof(addresses[i]);
                if (bind(sockets[i], (struct sockaddr *) &addresses[i], sizeof(addresses[i])) != 0 ||
                    getsockname(sockets[i], (struct sockaddr *) &addresses[i], &length) != 0) {
                        closesocket(sockets[i]);
                        goto out;
                }
                nonblock(sockets[i]);
                counters[i].total = &total;
                ios[i] = io_create(monitor, sockets[i], MEDUSA_IO_EVENT_OUT, 1, counter_onevent, &counters[i]);
                if (ios[i] == NULL) {
                        closesocket(sockets[i]);
                        goto out;
                }
        }
        while (total < MANY) {
                if (run_for(monitor, 0.05) != 0) {
                        goto out;
                }
                if (now() > g_deadline - 5.0) {
                        fprintf(stderr, "    out is reported for %d of %d sockets\n", total, MANY);
                        goto out;
                }
        }
        step("many, datagrams");
        for (i = 0; i < MANY; i++) {
                counters[i].in = 0;
                medusa_io_set_events(ios[i], MEDUSA_IO_EVENT_IN);
        }
        if (run_for(monitor, 0.1) != 0) {
                goto out;
        }
        sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sender == INVALID_SOCKET) {
                goto out;
        }
        for (i = 0; i < MANY; i++) {
                sendto(sender, "x", 1, 0, (struct sockaddr *) &addresses[i], sizeof(addresses[i]));
        }
        while (1) {
                int received;
                received = 0;
                for (i = 0; i < MANY; i++) {
                        received += (counters[i].in > 0) ? 1 : 0;
                }
                if (received == MANY) {
                        break;
                }
                if (now() > g_deadline - 5.0) {
                        fprintf(stderr, "    in is reported for %d of %d sockets\n", received, MANY);
                        goto out;
                }
                if (run_for(monitor, 0.05) != 0) {
                        goto out;
                }
        }
        rc = 0;
out:    if (sender != INVALID_SOCKET) {
                closesocket(sender);
        }
        medusa_monitor_destroy(monitor);
        return rc;
}

/* 8. reuse */

static int test_reuse (int mode)
{
        int rc;
        int events;
        SOCKET a;
        SOCKET b;
        struct counter ca;
        struct counter cb;
        struct medusa_io *io;
        struct medusa_monitor *monitor;

        step("reuse");
        rc = -1;
        memset(&ca, 0, sizeof(ca));
        memset(&cb, 0, sizeof(cb));
        monitor = monitor_create(mode);
        if (monitor == NULL) {
                return -1;
        }
        a = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (a == INVALID_SOCKET) {
                goto out;
        }
        nonblock(a);
        io = io_create(monitor, a, MEDUSA_IO_EVENT_OUT, 1, counter_onevent, &ca);
        if (io == NULL) {
                closesocket(a);
                goto out;
        }
        if (run_until(monitor, &ca.out, 2.0) != 0) {
                goto out;
        }
        medusa_io_destroy(io);
        if (run_until(monitor, &ca.destroyed, 2.0) != 0) {
                goto out;
        }
        events = ca.any;
        b = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (b == INVALID_SOCKET) {
                goto out;
        }
        fprintf(stderr, "      handle: %d, reused: %s\n", (int) b, (b == a) ? "yes" : "no");
        nonblock(b);
        if (io_create(monitor, b, MEDUSA_IO_EVENT_OUT, 1, counter_onevent, &cb) == NULL) {
                closesocket(b);
                goto out;
        }
        if (run_until(monitor, &cb.out, 2.0) != 0) {
                goto out;
        }
        if (run_for(monitor, 0.2) != 0) {
                goto out;
        }
        if (ca.any != events) {
                fprintf(stderr, "    a destroyed io got events\n");
                goto out;
        }
        rc = 0;
out:    medusa_monitor_destroy(monitor);
        return rc;
}

/* kill switch */

static int test_disable (void)
{
        int rc;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        step("disable");
        rc = -1;
        _putenv("MEDUSA_POLL_IOCP_DISABLE=1");
        medusa_monitor_init_options_default(&options);
        options.poll.type = MEDUSA_MONITOR_POLL_IOCP;
        monitor = medusa_monitor_create_with_options(&options);
        if (!MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "    iocp backend is created while it is disabled\n");
                medusa_monitor_destroy(monitor);
                goto out;
        }
        options.poll.type = MEDUSA_MONITOR_POLL_DEFAULT;
        monitor = medusa_monitor_create_with_options(&options);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "    default monitor is not created while iocp is disabled\n");
                goto out;
        }
        if (medusa_monitor_get_wakeup_type(monitor) != MEDUSA_MONITOR_WAKEUP_NONE) {
                fprintf(stderr, "    default did not fall back to wsapoll, wakeup: %s\n", medusa_monitor_wakeup_type_string(medusa_monitor_get_wakeup_type(monitor)));
                medusa_monitor_destroy(monitor);
                goto out;
        }
        medusa_monitor_destroy(monitor);
        rc = 0;
out:    _putenv("MEDUSA_POLL_IOCP_DISABLE=");
        return rc;
}

static int test_default (void)
{
        int rc;
        struct medusa_monitor *monitor;
        step("default is iocp");
        monitor = medusa_monitor_create();
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "    medusa_monitor_create failed\n");
                return -1;
        }
        rc = (medusa_monitor_get_wakeup_type(monitor) == MEDUSA_MONITOR_WAKEUP_BACKEND) ? 0 : -1;
        if (rc != 0) {
                fprintf(stderr, "    default monitor does not use iocp\n");
        }
        medusa_monitor_destroy(monitor);
        return rc;
}

int main (int argc, char *argv[])
{
        int mode;
        int failed;
        pthread_t watchdog;
        WSADATA wsadata;

        (void) argc;
        (void) argv;

        WSAStartup(MAKEWORD(2, 2), &wsadata);
        pthread_create(&watchdog, NULL, watchdog_routine, NULL);

        failed = 0;
        failed += (test_default() != 0);
        for (mode = MODE_IOCP; mode <= MODE_WSAPOLL; mode++) {
                fprintf(stderr, "testing: %s\n", mode_string(mode));
                failed += (test_echo(mode) != 0);
                failed += (test_reset(mode) != 0);
                failed += (test_connect_fail(mode) != 0);
                failed += (test_mod(mode) != 0);
                failed += (test_destroy_pending(mode) != 0);
                failed += (test_local_close(mode) != 0);
                failed += (test_many(mode) != 0);
                failed += (test_reuse(mode) != 0);
        }
        failed += (test_disable() != 0);
        g_deadline = 0;

        if (failed) {
                fprintf(stderr, "failed: %d\n", failed);
                return -1;
        }
        fprintf(stderr, "success\n");
        return 0;
}

#else

int main (int argc, char *argv[])
{
        (void) argc;
        (void) argv;
        fprintf(stderr, "skipping, windows only\n");
        return 0;
}

#endif
