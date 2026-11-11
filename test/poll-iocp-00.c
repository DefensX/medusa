
/*
 * windows poll backend checks, the same scenarios run against the default
 * backend (afd/iocp), explicit iocp, explicit wsapoll and the default with
 * the iocp backend disabled through the environment (wsapoll with a socket
 * pair wakeup):
 *
 *   1. a loop blocked without any io is woken by another thread
 *   2. loopback tcp: accept, read, write, peer close reported as hup/in
 *   3. an io destroyed by another thread while its poll is pending
 *   4. a refused non blocking connect reports out/err with SO_ERROR set
 *
 * the switch groups force the runtime switch from iocp to wsapoll that an lsp
 * layered socket (add:N) or a refused afd poll (arm:N) would cause, through
 * MEDUSA_POLL_IOCP_TEST_SWITCH, and check that every io registered before the
 * switch keeps working, new ios work, and the wakeup moved to a socket pair.
 *
 * usage: poll-iocp-00.exe [all|basic|switch], default all
 *
 * every step is printed before it starts. a watchdog names the step, dumps
 * the stacks of the test threads and exits with 3 if a step takes too long,
 * a crash prints the exception and exits with 4. MEDUSA_TEST_DEBUG=1 enables
 * medusa debug logs, 2 enables traces.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#if defined(__WINDOWS__)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <pthread.h>

#include "medusa/error.h"
#include "medusa/debug.h"
#include "medusa/io.h"
#include "medusa/monitor.h"

#define TEST_TIMEOUT_MS         5000
#define WATCHDOG_TIMEOUT_MS     10000
#define MAX_THREADS             256

static const char * volatile g_backend = "";
static const char * volatile g_step = "start";
static volatile ULONGLONG g_step_start;

static int g_debug;
static volatile int g_switched;

static HANDLE g_threads[MAX_THREADS];
static const char *g_thread_names[MAX_THREADS];
static volatile LONG g_nthreads;

/* bypasses the crt stream lock, which a stuck thread may hold */
static void raw_printf (const char *fmt, ...)
{
        int length;
        DWORD written;
        va_list ap;
        char buffer[512];
        va_start(ap, fmt);
        length = vsnprintf(buffer, sizeof(buffer), fmt, ap);
        va_end(ap);
        if (length < 0) {
                return;
        }
        if (length > (int) sizeof(buffer) - 1) {
                length = sizeof(buffer) - 1;
        }
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), buffer, length, &written, NULL);
}

static void register_thread (const char *name)
{
        LONG index;
        HANDLE handle;
        if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle, 0, FALSE, DUPLICATE_SAME_ACCESS) == FALSE) {
                return;
        }
        index = InterlockedIncrement(&g_nthreads) - 1;
        if (index >= MAX_THREADS) {
                CloseHandle(handle);
                return;
        }
        g_thread_names[index] = name;
        g_threads[index]      = handle;
}

static void print_frame (int depth, DWORD64 address)
{
        HMODULE module;
        char path[MAX_PATH];
        const char *base;
        module = NULL;
        path[0] = '\0';
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) (ULONG_PTR) address, &module) && module != NULL) {
                GetModuleFileNameA(module, path, sizeof(path));
        }
        base = strrchr(path, '\\');
        base = (base != NULL) ? base + 1 : path;
        raw_printf("    #%02d %s+0x%llx\n", depth, (base[0] != '\0') ? base : "?", (unsigned long long) (address - (DWORD64) (ULONG_PTR) module));
}

static void dump_thread (HANDLE thread, const char *name)
{
        int depth;
        CONTEXT context;
        DWORD64 image;
        PVOID handler;
        DWORD64 frame;
        PRUNTIME_FUNCTION function;
        if (SuspendThread(thread) == (DWORD) -1) {
                return;
        }
        memset(&context, 0, sizeof(context));
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &context) == FALSE) {
                return;
        }
        raw_printf("  thread: %s\n", name);
        for (depth = 0; depth < 40 && context.Rip != 0; depth++) {
                print_frame(depth, context.Rip);
                image = 0;
                function = RtlLookupFunctionEntry(context.Rip, &image, NULL);
                if (function == NULL) {
                        /* leaf function, the return address is on top of the stack */
                        context.Rip = *(DWORD64 *) (ULONG_PTR) context.Rsp;
                        context.Rsp += 8;
                        continue;
                }
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler, &frame, NULL);
        }
}

static void step (const char *name)
{
        g_step = name;
        g_step_start = GetTickCount64();
        fprintf(stderr, "  [%s] %s\n", g_backend, name);
        fflush(stderr);
}

static void * watchdog_thread (void *arg)
{
        (void) arg;
        while (1) {
                Sleep(500);
                if (GetTickCount64() - g_step_start > WATCHDOG_TIMEOUT_MS) {
                        LONG i;
                        raw_printf("STUCK: backend: %s, step: %s, for %d ms\n", g_backend, g_step, WATCHDOG_TIMEOUT_MS);
                        raw_printf("  image base: %p\n", (void *) GetModuleHandle(NULL));
                        for (i = 0; i < g_nthreads && i < MAX_THREADS; i++) {
                                DWORD code;
                                if (GetExitCodeThread(g_threads[i], &code) && code != STILL_ACTIVE) {
                                        continue;
                                }
                                dump_thread(g_threads[i], g_thread_names[i]);
                        }
                        _exit(3);
                }
        }
        return NULL;
}

static LONG WINAPI crash_handler (EXCEPTION_POINTERS *pointers)
{
        fprintf(stderr, "CRASH: backend: %s, step: %s, code: 0x%08lx, address: %p, image base: %p\n",
                g_backend, g_step,
                (unsigned long) pointers->ExceptionRecord->ExceptionCode,
                pointers->ExceptionRecord->ExceptionAddress,
                (void *) GetModuleHandle(NULL));
        if (pointers->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            pointers->ExceptionRecord->NumberParameters >= 2) {
                fprintf(stderr, "CRASH: %s at %p\n",
                        (pointers->ExceptionRecord->ExceptionInformation[0] == 0) ? "read" : "write",
                        (void *) pointers->ExceptionRecord->ExceptionInformation[1]);
        }
        fflush(stderr);
        _exit(4);
        return EXCEPTION_EXECUTE_HANDLER;
}

/* run the loop until *done is set, with a deadline, so a missing event fails
 * the test instead of hanging it. run_timeout returns the running flag, not
 * an event count, so it can not be used to detect idleness. */
static int run_until (struct medusa_monitor *monitor, volatile int *done, DWORD timeout)
{
        int rc;
        ULONGLONG start;
        start = GetTickCount64();
        while (*done == 0) {
                if (GetTickCount64() - start > timeout) {
                        return -ETIMEDOUT;
                }
                rc = medusa_monitor_run_timeout(monitor, 0.2);
                if (rc < 0) {
                        return rc;
                }
        }
        return 0;
}

struct breaker {
        struct medusa_monitor *monitor;
        DWORD delay;
};

static void * breaker_thread (void *arg)
{
        struct breaker *breaker = (struct breaker *) arg;
        register_thread("breaker");
        Sleep(breaker->delay);
        medusa_monitor_break(breaker->monitor);
        return NULL;
}

static struct medusa_monitor * monitor_create (unsigned int poll)
{
        struct medusa_monitor_init_options options;
        medusa_monitor_init_options_default(&options);
        options.poll.type = poll;
        return medusa_monitor_create_with_options(&options);
}

static int test_wakeup (unsigned int poll)
{
        int rc;
        ULONGLONG start;
        ULONGLONG elapsed;
        pthread_t thread;
        struct breaker breaker;
        struct medusa_monitor *monitor;

        step("wakeup: create monitor");
        monitor = monitor_create(poll);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "  wakeup: can not create monitor\n");
                return -1;
        }
        step("wakeup: blocked run, break from another thread");
        breaker.monitor = monitor;
        breaker.delay   = 200;
        pthread_create(&thread, NULL, breaker_thread, &breaker);
        start = GetTickCount64();
        /* nothing to wait for, only the wakeup can end this */
        rc = medusa_monitor_run(monitor);
        elapsed = GetTickCount64() - start;
        pthread_join(thread, NULL);
        step("wakeup: destroy monitor");
        medusa_monitor_destroy(monitor);
        if (rc < 0) {
                fprintf(stderr, "  wakeup: run failed, rc: %d\n", rc);
                return -1;
        }
        if (elapsed > TEST_TIMEOUT_MS) {
                fprintf(stderr, "  wakeup: took %llu ms\n", (unsigned long long) elapsed);
                return -1;
        }
        return 0;
}

struct tcp_state {
        SOCKET listener;
        SOCKET client;
        SOCKET server;
        struct medusa_io *listener_io;
        struct medusa_io *server_io;
        volatile int accepted;
        volatile int accepts;
        volatile int accepted2;
        volatile int received;
        volatile int closed;
        volatile int echoed;
        char buffer[64];
};

static int server_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        struct tcp_state *state = (struct tcp_state *) context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        if (events & (MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_HUP)) {
                rc = recv(state->server, state->buffer + state->received, sizeof(state->buffer) - state->received - 1, 0);
                if (rc > 0) {
                        state->received += rc;
                        send(state->server, state->buffer + state->received - rc, rc, 0);
                        state->echoed = 1;
                } else if (rc == 0) {
                        state->closed = 1;
                        medusa_io_destroy(io);
                        state->server_io = NULL;
                } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
                        state->closed = -1;
                        medusa_io_destroy(io);
                        state->server_io = NULL;
                }
        }
        return 0;
}

static int listener_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        u_long nonblocking;
        struct tcp_state *state = (struct tcp_state *) context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        if (events & MEDUSA_IO_EVENT_IN) {
                SOCKET s;
                s = accept(state->listener, NULL, NULL);
                if (s == INVALID_SOCKET) {
                        return 0;
                }
                state->accepts += 1;
                if (state->accepted) {
                        /* later connections only prove the listener still works */
                        closesocket(s);
                        state->accepted2 = 1;
                        return 0;
                }
                state->server = s;
                nonblocking = 1;
                ioctlsocket(state->server, FIONBIO, &nonblocking);
                state->server_io = medusa_io_create(medusa_io_get_monitor(io), (int) state->server, server_io_onevent, state);
                if (MEDUSA_IS_ERR_OR_NULL(state->server_io)) {
                        return -1;
                }
                medusa_io_set_clodestroy(state->server_io, 1);
                medusa_io_set_events(state->server_io, MEDUSA_IO_EVENT_IN);
                medusa_io_set_enabled(state->server_io, 1);
                state->accepted = 1;
        }
        return 0;
}

static int test_tcp (unsigned int poll)
{
        int rc;
        int length;
        char echo[16];
        DWORD timeout;
        struct sockaddr_in sin;
        struct tcp_state state;
        struct medusa_monitor *monitor;

        memset(&state, 0, sizeof(state));
        state.listener = INVALID_SOCKET;
        state.client   = INVALID_SOCKET;
        state.server   = INVALID_SOCKET;

        step("tcp: create monitor");
        monitor = monitor_create(poll);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "  tcp: can not create monitor\n");
                return -1;
        }

        memset(&sin, 0, sizeof(sin));
        sin.sin_family      = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        state.listener = socket(AF_INET, SOCK_STREAM, 0);
        bind(state.listener, (struct sockaddr *) &sin, sizeof(sin));
        listen(state.listener, 4);
        length = sizeof(sin);
        getsockname(state.listener, (struct sockaddr *) &sin, &length);

        state.listener_io = medusa_io_create(monitor, (int) state.listener, listener_io_onevent, &state);
        medusa_io_set_events(state.listener_io, MEDUSA_IO_EVENT_IN);
        medusa_io_set_enabled(state.listener_io, 1);

        step("tcp: connect");
        state.client = socket(AF_INET, SOCK_STREAM, 0);
        timeout = TEST_TIMEOUT_MS;
        setsockopt(state.client, SOL_SOCKET, SO_RCVTIMEO, (const char *) &timeout, sizeof(timeout));
        rc = connect(state.client, (struct sockaddr *) &sin, sizeof(sin));
        if (rc != 0) {
                fprintf(stderr, "  tcp: connect failed: %d\n", WSAGetLastError());
                goto bail;
        }

        step("tcp: wait for accept event");
        rc = run_until(monitor, &state.accepted, TEST_TIMEOUT_MS);
        if (rc < 0) {
                fprintf(stderr, "  tcp: no accept event, rc: %d\n", rc);
                goto bail;
        }

        step("tcp: wait for read event");
        send(state.client, "hello", 5, 0);
        rc = run_until(monitor, &state.echoed, TEST_TIMEOUT_MS);
        if (rc < 0 || state.received != 5 || memcmp(state.buffer, "hello", 5) != 0) {
                fprintf(stderr, "  tcp: no read event, rc: %d, received: %d\n", rc, state.received);
                goto bail;
        }

        step("tcp: client reads echo");
        rc = recv(state.client, echo, sizeof(echo), 0);
        if (rc != 5 || memcmp(echo, "hello", 5) != 0) {
                fprintf(stderr, "  tcp: client echo failed, rc: %d\n", rc);
                goto bail;
        }

        step("tcp: wait for peer close event");
        shutdown(state.client, SD_SEND);
        rc = run_until(monitor, &state.closed, TEST_TIMEOUT_MS);
        if (rc < 0 || state.closed != 1) {
                fprintf(stderr, "  tcp: peer close not seen, rc: %d, closed: %d\n", rc, state.closed);
                goto bail;
        }

        step("tcp: destroy monitor");
        closesocket(state.client);
        medusa_monitor_destroy(monitor);
        closesocket(state.listener);
        return 0;
bail:
        if (state.client != INVALID_SOCKET) {
                closesocket(state.client);
        }
        medusa_monitor_destroy(monitor);
        if (state.listener != INVALID_SOCKET) {
                closesocket(state.listener);
        }
        return -1;
}

struct destroyer {
        struct medusa_io *io;
};

static int idle_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        volatile int *destroyed = (volatile int *) context;
        (void) io;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                *destroyed += 1;
        }
        return 0;
}

static void * destroyer_thread (void *arg)
{
        struct destroyer *destroyer = (struct destroyer *) arg;
        register_thread("destroyer");
        Sleep(200);
        /* the loop thread is blocked with a poll pending on this socket */
        medusa_io_destroy(destroyer->io);
        return NULL;
}

static int test_destroy_pending (unsigned int poll)
{
        int i;
        int rc;
        volatile int destroyed;
        SOCKET s;
        pthread_t thread;
        struct sockaddr_in sin;
        struct destroyer destroyer;
        struct medusa_monitor *monitor;

        step("destroy: create monitor");
        monitor = monitor_create(poll);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "  destroy: can not create monitor\n");
                return -1;
        }
        step("destroy: 16 rounds of destroy from another thread while pending");
        for (i = 0; i < 16; i++) {
                destroyed = 0;
                memset(&sin, 0, sizeof(sin));
                sin.sin_family      = AF_INET;
                sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                s = socket(AF_INET, SOCK_DGRAM, 0);
                bind(s, (struct sockaddr *) &sin, sizeof(sin));
                destroyer.io = medusa_io_create(monitor, (int) s, idle_io_onevent, (void *) &destroyed);
                medusa_io_set_clodestroy(destroyer.io, 1);
                /* never becomes readable */
                medusa_io_set_events(destroyer.io, MEDUSA_IO_EVENT_IN);
                medusa_io_set_enabled(destroyer.io, 1);
                pthread_create(&thread, NULL, destroyer_thread, &destroyer);
                rc = run_until(monitor, &destroyed, TEST_TIMEOUT_MS);
                pthread_join(thread, NULL);
                if (rc < 0 || destroyed != 1) {
                        fprintf(stderr, "  destroy: round: %d, rc: %d, destroyed: %d\n", i, rc, destroyed);
                        goto bail;
                }
        }
        step("destroy: destroy monitor");
        medusa_monitor_destroy(monitor);
        return 0;
bail:   medusa_monitor_destroy(monitor);
        return -1;
}

struct connect_state {
        volatile int done;
        unsigned int events;
};

static int connect_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        struct connect_state *state = (struct connect_state *) context;
        (void) io;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                return 0;
        }
        state->events |= events;
        if (events & (MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_ERR | MEDUSA_IO_EVENT_HUP)) {
                state->done = 1;
        }
        return 0;
}

static int test_connect_fail (unsigned int poll)
{
        int rc;
        int error;
        int length;
        SOCKET s;
        SOCKET closed;
        u_long nonblocking;
        struct sockaddr_in sin;
        struct connect_state state;
        struct medusa_io *io;
        struct medusa_monitor *monitor;

        step("connect: create monitor");
        monitor = monitor_create(poll);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "  connect: can not create monitor\n");
                return -1;
        }

        /* grab a free port and release it, nothing listens there afterwards */
        memset(&sin, 0, sizeof(sin));
        sin.sin_family      = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        closed = socket(AF_INET, SOCK_STREAM, 0);
        bind(closed, (struct sockaddr *) &sin, sizeof(sin));
        length = sizeof(sin);
        getsockname(closed, (struct sockaddr *) &sin, &length);
        closesocket(closed);

        step("connect: non blocking connect to a closed port");
        s = socket(AF_INET, SOCK_STREAM, 0);
        nonblocking = 1;
        ioctlsocket(s, FIONBIO, &nonblocking);
        rc = connect(s, (struct sockaddr *) &sin, sizeof(sin));
        if (rc == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
                fprintf(stderr, "  connect: unexpected connect result: %d, %d\n", rc, WSAGetLastError());
                closesocket(s);
                medusa_monitor_destroy(monitor);
                return -1;
        }

        memset(&state, 0, sizeof(state));
        io = medusa_io_create(monitor, (int) s, connect_io_onevent, &state);
        medusa_io_set_clodestroy(io, 1);
        medusa_io_set_events(io, MEDUSA_IO_EVENT_OUT);
        medusa_io_set_enabled(io, 1);

        step("connect: wait for failure event");
        /* windows retries refused loopback syns for about two seconds */
        rc = run_until(monitor, &state.done, TEST_TIMEOUT_MS * 2);

        error = 0;
        length = sizeof(error);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *) &error, &length);
        step("connect: destroy monitor");
        medusa_monitor_destroy(monitor);

        if (rc < 0) {
                fprintf(stderr, "  connect: no event for refused connect, rc: %d\n", rc);
                return -1;
        }
        if (error == 0) {
                fprintf(stderr, "  connect: SO_ERROR is 0 after refused connect, events: 0x%x\n", state.events);
                return -1;
        }
        fprintf(stderr, "  connect: events: 0x%x, SO_ERROR: %d\n", state.events, error);
        return 0;
}

/* medusa logs come through here, the backend reports the switch at info level */
static int debug_callback (void *context, const char *format, ...)
{
        va_list ap;
        char buffer[1024];
        (void) context;
        va_start(ap, format);
        vsnprintf(buffer, sizeof(buffer), format, ap);
        va_end(ap);
        if (strstr(buffer, "switching to wsapoll backend") != NULL) {
                g_switched = 1;
        }
        if (g_debug ||
            strstr(buffer, ":error :") != NULL ||
            strstr(buffer, "switching to wsapoll backend") != NULL) {
                fputs(buffer, stderr);
        }
        return 0;
}

struct udp_state {
        SOCKET socket;
        volatile int received;
        volatile int destroyed;
};

static int udp_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        char buffer[64];
        struct udp_state *state = (struct udp_state *) context;
        (void) io;
        (void) param;
        if (events & MEDUSA_IO_EVENT_DESTROY) {
                state->destroyed = 1;
                return 0;
        }
        if (events & MEDUSA_IO_EVENT_IN) {
                while (1) {
                        rc = recv(state->socket, buffer, sizeof(buffer), 0);
                        if (rc <= 0) {
                                break;
                        }
                        state->received += 1;
                }
        }
        return 0;
}

static int test_switch (const char *trigger)
{
        int rc;
        int length;
        char env[64];
        char echo[16];
        DWORD timeout;
        u_long nonblocking;
        ULONGLONG start;
        ULONGLONG elapsed;
        SOCKET client2;
        SOCKET sender;
        pthread_t thread;
        struct breaker breaker;
        struct destroyer destroyer;
        struct sockaddr_in sin;
        struct sockaddr_in uin;
        struct tcp_state state;
        struct udp_state udp;
        struct medusa_io *udp_io;
        struct medusa_monitor *monitor;

        memset(&state, 0, sizeof(state));
        memset(&udp, 0, sizeof(udp));
        state.listener = INVALID_SOCKET;
        state.client   = INVALID_SOCKET;
        state.server   = INVALID_SOCKET;
        udp.socket     = INVALID_SOCKET;
        client2        = INVALID_SOCKET;
        sender         = INVALID_SOCKET;
        g_switched     = 0;

        step("switch: create monitor");
        snprintf(env, sizeof(env), "MEDUSA_POLL_IOCP_TEST_SWITCH=%s", trigger);
        _putenv(env);
        monitor = monitor_create(MEDUSA_MONITOR_POLL_DEFAULT);
        /* only this monitor gets the hook, it is read when the backend is created */
        _putenv("MEDUSA_POLL_IOCP_TEST_SWITCH=");
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "  switch: can not create monitor\n");
                return -1;
        }

        memset(&sin, 0, sizeof(sin));
        sin.sin_family      = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        state.listener = socket(AF_INET, SOCK_STREAM, 0);
        bind(state.listener, (struct sockaddr *) &sin, sizeof(sin));
        listen(state.listener, 4);
        length = sizeof(sin);
        getsockname(state.listener, (struct sockaddr *) &sin, &length);
        state.listener_io = medusa_io_create(monitor, (int) state.listener, listener_io_onevent, &state);
        medusa_io_set_events(state.listener_io, MEDUSA_IO_EVENT_IN);
        medusa_io_set_enabled(state.listener_io, 1);

        step("switch: connect, accept and echo before the switch");
        state.client = socket(AF_INET, SOCK_STREAM, 0);
        timeout = TEST_TIMEOUT_MS;
        setsockopt(state.client, SOL_SOCKET, SO_RCVTIMEO, (const char *) &timeout, sizeof(timeout));
        if (connect(state.client, (struct sockaddr *) &sin, sizeof(sin)) != 0) {
                fprintf(stderr, "  switch: connect failed: %d\n", WSAGetLastError());
                goto bail;
        }
        rc = run_until(monitor, &state.accepted, TEST_TIMEOUT_MS);
        if (rc < 0) {
                fprintf(stderr, "  switch: no accept event, rc: %d\n", rc);
                goto bail;
        }
        send(state.client, "hello", 5, 0);
        rc = run_until(monitor, &state.echoed, TEST_TIMEOUT_MS);
        if (rc < 0 || recv(state.client, echo, sizeof(echo), 0) != 5 || memcmp(echo, "hello", 5) != 0) {
                fprintf(stderr, "  switch: echo before the switch failed, rc: %d\n", rc);
                goto bail;
        }

        step("switch: add a udp io and wait for the switch");
        udp.socket = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&uin, 0, sizeof(uin));
        uin.sin_family      = AF_INET;
        uin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(udp.socket, (struct sockaddr *) &uin, sizeof(uin));
        length = sizeof(uin);
        getsockname(udp.socket, (struct sockaddr *) &uin, &length);
        nonblocking = 1;
        ioctlsocket(udp.socket, FIONBIO, &nonblocking);
        udp_io = medusa_io_create(monitor, (int) udp.socket, udp_io_onevent, &udp);
        medusa_io_set_clodestroy(udp_io, 1);
        medusa_io_set_events(udp_io, MEDUSA_IO_EVENT_IN);
        medusa_io_set_enabled(udp_io, 1);
        rc = run_until(monitor, &g_switched, TEST_TIMEOUT_MS);
        if (rc < 0) {
                fprintf(stderr, "  switch: the backend did not switch to wsapoll, rc: %d\n", rc);
                goto bail;
        }

        step("switch: echo on the connection accepted before the switch");
        state.echoed = 0;
        send(state.client, "again", 5, 0);
        rc = run_until(monitor, &state.echoed, TEST_TIMEOUT_MS);
        if (rc < 0 || state.received != 10 || memcmp(state.buffer + 5, "again", 5) != 0) {
                fprintf(stderr, "  switch: server did not read after the switch, rc: %d, received: %d\n", rc, state.received);
                goto bail;
        }
        rc = recv(state.client, echo, sizeof(echo), 0);
        if (rc != 5 || memcmp(echo, "again", 5) != 0) {
                fprintf(stderr, "  switch: client echo after the switch failed, rc: %d\n", rc);
                goto bail;
        }

        step("switch: second accept on the listener added before the switch");
        client2 = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(client2, (struct sockaddr *) &sin, sizeof(sin)) != 0) {
                fprintf(stderr, "  switch: second connect failed: %d\n", WSAGetLastError());
                goto bail;
        }
        rc = run_until(monitor, &state.accepted2, TEST_TIMEOUT_MS);
        if (rc < 0) {
                fprintf(stderr, "  switch: no second accept event, rc: %d\n", rc);
                goto bail;
        }

        step("switch: datagram on the udp io");
        sender = socket(AF_INET, SOCK_DGRAM, 0);
        sendto(sender, "ping", 4, 0, (struct sockaddr *) &uin, sizeof(uin));
        rc = run_until(monitor, &udp.received, TEST_TIMEOUT_MS);
        if (rc < 0) {
                fprintf(stderr, "  switch: no read event on the udp io, rc: %d\n", rc);
                goto bail;
        }

        step("switch: break a blocked loop from another thread (socket pair wakeup)");
        breaker.monitor = monitor;
        breaker.delay   = 200;
        pthread_create(&thread, NULL, breaker_thread, &breaker);
        start = GetTickCount64();
        rc = medusa_monitor_run(monitor);
        elapsed = GetTickCount64() - start;
        pthread_join(thread, NULL);
        if (rc < 0 || elapsed > TEST_TIMEOUT_MS) {
                fprintf(stderr, "  switch: wakeup after the switch failed, rc: %d, took: %llu ms\n", rc, (unsigned long long) elapsed);
                goto bail;
        }

        step("switch: destroy an io from another thread");
        destroyer.io = udp_io;
        pthread_create(&thread, NULL, destroyer_thread, &destroyer);
        rc = run_until(monitor, &udp.destroyed, TEST_TIMEOUT_MS);
        pthread_join(thread, NULL);
        if (rc < 0) {
                fprintf(stderr, "  switch: io destroy after the switch not processed, rc: %d\n", rc);
                goto bail;
        }

        step("switch: peer close on the connection accepted before the switch");
        shutdown(state.client, SD_SEND);
        rc = run_until(monitor, &state.closed, TEST_TIMEOUT_MS);
        if (rc < 0 || state.closed != 1) {
                fprintf(stderr, "  switch: peer close not seen, rc: %d, closed: %d\n", rc, state.closed);
                goto bail;
        }

        step("switch: destroy monitor");
        closesocket(sender);
        closesocket(client2);
        closesocket(state.client);
        medusa_monitor_destroy(monitor);
        closesocket(state.listener);
        return 0;
bail:
        if (sender != INVALID_SOCKET) {
                closesocket(sender);
        }
        if (client2 != INVALID_SOCKET) {
                closesocket(client2);
        }
        if (state.client != INVALID_SOCKET) {
                closesocket(state.client);
        }
        medusa_monitor_destroy(monitor);
        if (state.listener != INVALID_SOCKET) {
                closesocket(state.listener);
        }
        return -1;
}

static int test_switch_group (const char *name, const char *trigger)
{
        g_backend = name;
        fprintf(stderr, "%s\n", name);
        return test_switch(trigger);
}

static int test_poll (const char *name, unsigned int poll)
{
        g_backend = name;
        fprintf(stderr, "%s\n", name);
        if (test_wakeup(poll) != 0) {
                return -1;
        }
        if (test_tcp(poll) != 0) {
                return -1;
        }
        if (test_destroy_pending(poll) != 0) {
                return -1;
        }
        if (test_connect_fail(poll) != 0) {
                return -1;
        }
        return 0;
}

int main (int argc, char *argv[])
{
        int basic;
        int sw;
        const char *mode;
        const char *debug;
        pthread_t watchdog;
        WSADATA wsadata;
        mode = (argc > 1) ? argv[1] : "all";
        basic = (strcmp(mode, "all") == 0 || strcmp(mode, "basic") == 0);
        sw    = (strcmp(mode, "all") == 0 || strcmp(mode, "switch") == 0);
        if (!basic && !sw) {
                fprintf(stderr, "usage: %s [all|basic|switch]\n", argv[0]);
                return 2;
        }
        setvbuf(stderr, NULL, _IONBF, 0);
        register_thread("main (loop)");
        SetUnhandledExceptionFilter(crash_handler);
        debug = getenv("MEDUSA_TEST_DEBUG");
        if (debug != NULL) {
                g_debug = 1;
                medusa_debug_level = (atoi(debug) >= 2) ? MEDUSA_DEBUG_LEVEL_TRACE : MEDUSA_DEBUG_LEVEL_DEBUG;
        } else {
                /* the switch is reported at info level */
                medusa_debug_level = MEDUSA_DEBUG_LEVEL_INFO;
        }
        medusa_debug_set_callback(debug_callback, NULL);
        g_step_start = GetTickCount64();
        pthread_create(&watchdog, NULL, watchdog_thread, NULL);
        WSAStartup(MAKEWORD(2, 2), &wsadata);
        if (basic) {
                if (test_poll("default", MEDUSA_MONITOR_POLL_DEFAULT) != 0) {
                        return 1;
                }
                if (test_poll("iocp", MEDUSA_MONITOR_POLL_IOCP) != 0) {
                        return 1;
                }
                if (test_poll("wsapoll", MEDUSA_MONITOR_POLL_WSAPOLL) != 0) {
                        return 1;
                }
        }
        if (sw) {
                if (test_switch_group("switch on add (lsp layered socket)", "add:3") != 0) {
                        return 1;
                }
                if (test_switch_group("switch on arm (afd poll refused)", "arm:5") != 0) {
                        return 1;
                }
        }
        if (basic) {
                /* last, the variable stays set for the rest of the process */
                _putenv("MEDUSA_POLL_IOCP_DISABLE=1");
                if (test_poll("default, iocp disabled", MEDUSA_MONITOR_POLL_DEFAULT) != 0) {
                        return 1;
                }
        }
        WSACleanup();
        fprintf(stderr, "success\n");
        return 0;
}

#else

int main (int argc, char *argv[])
{
        (void) argc;
        (void) argv;
        return 0;
}

#endif
