
/*
 * readiness polling on top of an i/o completion port.
 *
 * every winsock socket is backed by the afd driver, and IOCTL_AFD_POLL is the
 * request WSAPoll() and select() use underneath. issuing it ourselves on an afd
 * helper handle that is associated with a completion port turns readiness into
 * completion packets, which gives epoll like behavior:
 *
 *   - add/mod/del are O(1), there is no fd array handed to the kernel per run
 *   - the monitor can be woken with PostQueuedCompletionStatus(), no socket pair
 *   - user sockets are never associated with our port, they stay untouched
 *
 * each poll request is one shot, sockets are re-armed with the current interest
 * at the start of every run, which keeps level triggered semantics identical to
 * the wsapoll backend.
 *
 * lifetimes: the per socket state is owned by this backend. del detaches it
 * from the medusa_io immediately (the io may be freed right after del returns),
 * cancels any pending request and parks the state on a dead list. the state is
 * freed only once the kernel has handed back its completion, since the kernel
 * writes into the io status block until then.
 *
 * fallbacks:
 *   - create probes tcp/udp over ipv4/ipv6 and runs a live poll; on any failure
 *     create returns NULL and the monitor picks the wsapoll backend.
 *   - if a socket added later is layered by an lsp that wants to see polling,
 *     or afd refuses a poll request, the backend migrates every registered io
 *     to an internal wsapoll backend and clears its wakeup hook, so the monitor
 *     switches back to a socket pair wakeup. nothing is reported to the user.
 *
 * test hook, both switch paths can be forced for testing on machines without
 * an lsp, read once when the backend is created:
 *   MEDUSA_POLL_IOCP_TEST_SWITCH=add:N  the Nth added socket is seen as layered
 *   MEDUSA_POLL_IOCP_TEST_SWITCH=arm:N  the Nth afd poll request fails
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>

#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <winternl.h>

#define MEDUSA_DEBUG_NAME       "poll-iocp"

#include "debug.h"
#include "queue.h"
#include "subject-struct.h"
#include "io.h"
#include "io-private.h"
#include "io-struct.h"
#include "monitor-private.h"

#include "poll-backend.h"
#include "poll-wsapoll.h"
#include "poll-iocp.h"

#if !defined(SIO_BASE_HANDLE)
#define SIO_BASE_HANDLE                 _WSAIOR(IOC_WS2, 34)
#endif
#if !defined(SIO_BSP_HANDLE_SELECT)
#define SIO_BSP_HANDLE_SELECT           _WSAIOR(IOC_WS2, 28)
#endif
#if !defined(SIO_BSP_HANDLE_POLL)
#define SIO_BSP_HANDLE_POLL             _WSAIOR(IOC_WS2, 29)
#endif

#define IOCP_STATUS_SUCCESS             ((NTSTATUS) 0x00000000L)
#define IOCP_STATUS_PENDING             ((NTSTATUS) 0x00000103L)
#define IOCP_STATUS_INVALID_HANDLE      ((NTSTATUS) 0xC0000008L)
#define IOCP_STATUS_CANCELLED           ((NTSTATUS) 0xC0000120L)
#define IOCP_STATUS_NOT_FOUND           ((NTSTATUS) 0xC0000225L)
#define IOCP_STATUS_NOT_SUPPORTED       ((NTSTATUS) 0xC00000BBL)
#define IOCP_NT_SUCCESS(status)         (((NTSTATUS) (status)) >= 0)
/* only error severity statuses fail without queueing a completion packet */
#define IOCP_NT_ERROR(status)           ((((ULONG) (status)) >> 30) == 3)

#define IOCP_FILE_OPEN                  0x00000001

#define IOCTL_AFD_POLL                  0x00012024

#define AFD_POLL_RECEIVE                0x0001
#define AFD_POLL_RECEIVE_EXPEDITED      0x0002
#define AFD_POLL_SEND                   0x0004
#define AFD_POLL_DISCONNECT             0x0008
#define AFD_POLL_ABORT                  0x0010
#define AFD_POLL_LOCAL_CLOSE            0x0020
#define AFD_POLL_ACCEPT                 0x0080
#define AFD_POLL_CONNECT_FAIL           0x0100

/* always requested: WSAPoll reports POLLHUP/POLLERR whether asked for or not,
 * and tcpsocket relies on hup arriving while read interest is disabled */
#define AFD_POLL_ALWAYS                 (AFD_POLL_DISCONNECT | AFD_POLL_ABORT | AFD_POLL_LOCAL_CLOSE | AFD_POLL_CONNECT_FAIL)

#define IOCP_KEY_AFD                    0
#define IOCP_KEY_WAKEUP                 1

#define IOCP_ENTRIES                    256
#define IOCP_PROBE_TIMEOUT_MS           2000
#define IOCP_DRAIN_TIMEOUT_MS           2000

#define MEDUSA_IO_EVENT_ALWAYS          (MEDUSA_IO_EVENT_ERR | MEDUSA_IO_EVENT_HUP | MEDUSA_IO_EVENT_NVAL)

typedef struct {
        HANDLE Handle;
        ULONG Events;
        NTSTATUS Status;
} afd_poll_handle_info_t;

typedef struct {
        LARGE_INTEGER Timeout;
        ULONG NumberOfHandles;
        ULONG Exclusive;
        afd_poll_handle_info_t Handles[1];
} afd_poll_info_t;

typedef NTSTATUS (NTAPI *nt_create_file_t) (PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength);
typedef NTSTATUS (NTAPI *nt_device_io_control_file_t) (HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);
typedef NTSTATUS (NTAPI *nt_cancel_io_file_ex_t) (HANDLE FileHandle, PIO_STATUS_BLOCK IoRequestToCancel, PIO_STATUS_BLOCK IoStatusBlock);

static struct {
        int loaded;
        nt_create_file_t nt_create_file;
        nt_device_io_control_file_t nt_device_io_control_file;
        nt_cancel_io_file_ex_t nt_cancel_io_file_ex;
} g_nt;

enum {
        SOCK_STATE_NONE         = 0,
        SOCK_STATE_LIVE         = 1,
        SOCK_STATE_DEAD         = 2
};

enum {
        RESOLVE_OK              = 0,
        RESOLVE_NOTSOCK         = 1,
        RESOLVE_LAYERED         = 2
};

TAILQ_HEAD(sock_list, sock);
struct sock {
        /* the kernel writes into iosb and poll_info until the request completes */
        IO_STATUS_BLOCK iosb;
        afd_poll_info_t poll_info;
        struct medusa_io *io;
        SOCKET base;
        int state;
        int invalid;
        int pending;
        int queued;
        ULONG pending_events;
        TAILQ_ENTRY(sock) list;
        TAILQ_ENTRY(sock) update;
};

struct dispatch {
        struct sock *sock;
        struct medusa_io *io;
        unsigned int events;
};

struct internal {
        struct medusa_poll_backend backend;
        HANDLE port;
        HANDLE afd;
        int wsa;
        struct sock_list live;
        struct sock_list dead;
        struct sock_list updates;
        OVERLAPPED_ENTRY entries[IOCP_ENTRIES];
        struct dispatch *dispatches;
        int ndispatches;
        int sdispatches;
        struct medusa_poll_backend *fallback;
        int (*onevent) (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
        struct {
                unsigned long add;
                unsigned long arm;
                unsigned long nadds;
                unsigned long narms;
        } test;
};

static void internal_lock (struct internal *internal)
{
        if (internal->backend.monitor != NULL) {
                medusa_monitor_lock(internal->backend.monitor);
        }
}

static void internal_unlock (struct internal *internal)
{
        if (internal->backend.monitor != NULL) {
                medusa_monitor_unlock(internal->backend.monitor);
        }
}

static int nt_load (void)
{
        HMODULE ntdll;
        if (g_nt.loaded == 1) {
                return 0;
        }
        ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == NULL) {
                return -1;
        }
        g_nt.nt_create_file            = (nt_create_file_t) (void (*) (void)) GetProcAddress(ntdll, "NtCreateFile");
        g_nt.nt_device_io_control_file = (nt_device_io_control_file_t) (void (*) (void)) GetProcAddress(ntdll, "NtDeviceIoControlFile");
        g_nt.nt_cancel_io_file_ex      = (nt_cancel_io_file_ex_t) (void (*) (void)) GetProcAddress(ntdll, "NtCancelIoFileEx");
        if (g_nt.nt_create_file == NULL ||
            g_nt.nt_device_io_control_file == NULL ||
            g_nt.nt_cancel_io_file_ex == NULL) {
                return -1;
        }
        g_nt.loaded = 1;
        return 0;
}

static HANDLE afd_open (HANDLE port)
{
        static const WCHAR name[] = L"\\Device\\Afd\\Medusa";
        HANDLE afd;
        NTSTATUS status;
        IO_STATUS_BLOCK iosb;
        UNICODE_STRING uname;
        OBJECT_ATTRIBUTES attributes;

        uname.Length        = sizeof(name) - sizeof(WCHAR);
        uname.MaximumLength = sizeof(name);
        uname.Buffer        = (PWSTR) name;

        memset(&attributes, 0, sizeof(attributes));
        attributes.Length     = sizeof(attributes);
        attributes.ObjectName = &uname;

        afd = NULL;
        status = g_nt.nt_create_file(&afd, SYNCHRONIZE, &attributes, &iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, IOCP_FILE_OPEN, 0, NULL, 0);
        if (status != IOCP_STATUS_SUCCESS || afd == NULL) {
                medusa_debugf("can not open afd device, status: 0x%08lx", (unsigned long) status);
                return NULL;
        }
        if (CreateIoCompletionPort(afd, port, IOCP_KEY_AFD, 0) == NULL) {
                medusa_debugf("can not associate afd device with port, error: %lu", GetLastError());
                CloseHandle(afd);
                return NULL;
        }
        SetFileCompletionNotificationModes(afd, FILE_SKIP_SET_EVENT_ON_HANDLE);
        return afd;
}

static int socket_ioctl_handle (SOCKET socket, DWORD ioctl, SOCKET *handle)
{
        int rc;
        DWORD bytes;
        SOCKET result;
        result = INVALID_SOCKET;
        rc = WSAIoctl(socket, ioctl, NULL, 0, &result, sizeof(result), &bytes, NULL, NULL);
        if (rc == SOCKET_ERROR) {
                return WSAGetLastError();
        }
        if (result == INVALID_SOCKET) {
                return WSAEINVAL;
        }
        *handle = result;
        return 0;
}

/*
 * find the afd socket to poll. an lsp that only forwards calls keeps the base
 * handle as its poll handle; an lsp that wants to observe or filter polling
 * returns its own handle for SIO_BSP_HANDLE_POLL/SELECT, or refuses to give out
 * the base handle at all. polling the base handle behind its back could report
 * readiness the lsp does not agree with, so such sockets are reported as
 * layered and handled by the wsapoll fallback instead.
 */
static int socket_resolve (SOCKET socket, SOCKET *base)
{
        int rc;
        SOCKET bh = INVALID_SOCKET;
        SOCKET ph = INVALID_SOCKET;
        rc = socket_ioctl_handle(socket, SIO_BASE_HANDLE, &bh);
        if (rc == WSAENOTSOCK) {
                return RESOLVE_NOTSOCK;
        }
        if (rc != 0) {
                medusa_debugf("SIO_BASE_HANDLE failed for socket: %d, error: %d", (int) socket, rc);
                return RESOLVE_LAYERED;
        }
        if (socket_ioctl_handle(socket, SIO_BSP_HANDLE_POLL, &ph) == 0) {
                if (ph != bh) {
                        medusa_debugf("socket: %d is layered, poll handle differs from base handle", (int) socket);
                        return RESOLVE_LAYERED;
                }
        } else if (socket_ioctl_handle(socket, SIO_BSP_HANDLE_SELECT, &ph) == 0) {
                if (ph != bh) {
                        medusa_debugf("socket: %d is layered, select handle differs from base handle", (int) socket);
                        return RESOLVE_LAYERED;
                }
        }
        *base = bh;
        return RESOLVE_OK;
}

static ULONG events_to_afd (unsigned int events)
{
        ULONG afd;
        afd = AFD_POLL_ALWAYS;
        if (events & MEDUSA_IO_EVENT_IN) {
                afd |= AFD_POLL_RECEIVE | AFD_POLL_ACCEPT;
        }
        if (events & MEDUSA_IO_EVENT_OUT) {
                afd |= AFD_POLL_SEND;
        }
        if (events & MEDUSA_IO_EVENT_PRI) {
                afd |= AFD_POLL_RECEIVE_EXPEDITED;
        }
        return afd;
}

/* mirrors what the wsapoll backend reports for the same socket states */
static unsigned int afd_to_events (ULONG afd)
{
        unsigned int events;
        events = 0;
        if (afd & (AFD_POLL_RECEIVE | AFD_POLL_ACCEPT)) {
                events |= MEDUSA_IO_EVENT_IN;
        }
        if (afd & AFD_POLL_RECEIVE_EXPEDITED) {
                events |= MEDUSA_IO_EVENT_PRI;
        }
        if (afd & AFD_POLL_SEND) {
                events |= MEDUSA_IO_EVENT_OUT;
        }
        if (afd & AFD_POLL_DISCONNECT) {
                /* WSAPoll reports a graceful close as POLLHUP */
                events |= MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_HUP;
        }
        if (afd & AFD_POLL_ABORT) {
                events |= MEDUSA_IO_EVENT_HUP | MEDUSA_IO_EVENT_ERR;
        }
        if (afd & AFD_POLL_CONNECT_FAIL) {
                /* connecting sockets pick the error up with SO_ERROR on the write event */
                events |= MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_ERR;
        }
        if (afd & AFD_POLL_LOCAL_CLOSE) {
                events |= MEDUSA_IO_EVENT_NVAL;
        }
        return events;
}

/*
 * msafd records the reason of a failed non blocking connect, read back with
 * SO_ERROR, when it processes the failure for select()/WSAPoll(). our poll
 * goes to afd directly, so let msafd see the failure once through the
 * documented path: a failed connect is reported in exceptfds.
 */
static void socket_sync_connect_error (SOCKET socket)
{
        fd_set wfds;
        fd_set efds;
        struct timeval tv;
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        FD_SET(socket, &wfds);
        FD_SET(socket, &efds);
        tv.tv_sec  = 0;
        tv.tv_usec = 0;
        select(0, NULL, &wfds, &efds, &tv);
}

static NTSTATUS sock_arm (struct internal *internal, struct sock *sock, ULONG events)
{
        NTSTATUS status;
        sock->poll_info.Timeout.QuadPart     = INT64_MAX;
        sock->poll_info.NumberOfHandles      = 1;
        sock->poll_info.Exclusive            = FALSE;
        sock->poll_info.Handles[0].Handle    = (HANDLE) sock->base;
        sock->poll_info.Handles[0].Events    = events;
        sock->poll_info.Handles[0].Status    = 0;
        sock->iosb.Status                    = IOCP_STATUS_PENDING;
        sock->iosb.Information               = 0;
        status = g_nt.nt_device_io_control_file(internal->afd, NULL, NULL, &sock->iosb, &sock->iosb, IOCTL_AFD_POLL, &sock->poll_info, sizeof(sock->poll_info), &sock->poll_info, sizeof(sock->poll_info));
        if (!IOCP_NT_ERROR(status)) {
                /* success, pending, informational and warning statuses all
                 * queue a completion packet, the sock must stay alive for it */
                sock->pending        = 1;
                sock->pending_events = events;
                return IOCP_STATUS_SUCCESS;
        }
        return status;
}

static void sock_cancel (struct internal *internal, struct sock *sock)
{
        NTSTATUS status;
        IO_STATUS_BLOCK iosb;
        if (sock->pending == 0) {
                return;
        }
        if (sock->iosb.Status != IOCP_STATUS_PENDING) {
                /* already completed, the packet is on its way */
                return;
        }
        status = g_nt.nt_cancel_io_file_ex(internal->afd, &sock->iosb, &iosb);
        if (status != IOCP_STATUS_SUCCESS && status != IOCP_STATUS_NOT_FOUND) {
                medusa_debugf("NtCancelIoFileEx failed, status: 0x%08lx", (unsigned long) status);
        }
}

static void sock_queue_update (struct internal *internal, struct sock *sock)
{
        if (sock->queued == 0) {
                TAILQ_INSERT_TAIL(&internal->updates, sock, update);
                sock->queued = 1;
        }
}

static void sock_dequeue_update (struct internal *internal, struct sock *sock)
{
        if (sock->queued == 1) {
                TAILQ_REMOVE(&internal->updates, sock, update);
                sock->queued = 0;
        }
}

/* the io may be freed as soon as this returns, nothing may point at it afterwards */
static void sock_kill (struct internal *internal, struct sock *sock)
{
        if (sock->state != SOCK_STATE_LIVE) {
                return;
        }
        sock_dequeue_update(internal, sock);
        if (sock->io != NULL) {
                sock->io->poll_backend_data = NULL;
                sock->io = NULL;
        }
        TAILQ_REMOVE(&internal->live, sock, list);
        TAILQ_INSERT_TAIL(&internal->dead, sock, list);
        sock->state = SOCK_STATE_DEAD;
        sock_cancel(internal, sock);
}

static void dead_reap (struct internal *internal)
{
        struct sock *sock;
        struct sock *nsock;
        TAILQ_FOREACH_SAFE(sock, &internal->dead, list, nsock) {
                if (sock->pending == 0) {
                        TAILQ_REMOVE(&internal->dead, sock, list);
                        free(sock);
                }
        }
}

static int dead_pending (struct internal *internal)
{
        struct sock *sock;
        TAILQ_FOREACH(sock, &internal->dead, list) {
                if (sock->pending != 0) {
                        return 1;
                }
        }
        return 0;
}

/*
 * wait until the kernel has returned every request that belongs to a dead
 * socket. only used when no live socket is left (migration and destroy), so
 * every afd packet dequeued here belongs to a dead socket.
 */
static void dead_drain (struct internal *internal, DWORD timeout)
{
        BOOL brc;
        ULONG i;
        ULONG count;
        ULONGLONG start;
        ULONGLONG elapsed;
        struct sock *sock;
        start = GetTickCount64();
        while (dead_pending(internal)) {
                elapsed = GetTickCount64() - start;
                if (elapsed >= timeout) {
                        break;
                }
                brc = GetQueuedCompletionStatusEx(internal->port, internal->entries, IOCP_ENTRIES, &count, (DWORD) (timeout - elapsed), FALSE);
                if (brc == FALSE) {
                        break;
                }
                for (i = 0; i < count; i++) {
                        if (internal->entries[i].lpOverlapped == NULL) {
                                continue;
                        }
                        sock = CONTAINING_RECORD((PIO_STATUS_BLOCK) internal->entries[i].lpOverlapped, struct sock, iosb);
                        sock->pending = 0;
                }
        }
        dead_reap(internal);
        if (!TAILQ_EMPTY(&internal->dead)) {
                int leaked;
                leaked = 0;
                TAILQ_FOREACH(sock, &internal->dead, list) {
                        leaked += 1;
                }
                /* never free memory the kernel may still write into */
                medusa_errorf("%d afd poll requests did not complete, leaking them", leaked);
                TAILQ_INIT(&internal->dead);
        }
}

static int dispatch_push (struct internal *internal, struct sock *sock, struct medusa_io *io, unsigned int events)
{
        if (internal->ndispatches + 1 > internal->sdispatches) {
                int sdispatches;
                struct dispatch *tmp;
                sdispatches = internal->sdispatches + IOCP_ENTRIES;
                tmp = (struct dispatch *) realloc(internal->dispatches, sizeof(struct dispatch) * sdispatches);
                if (tmp == NULL) {
                        return -1;
                }
                internal->dispatches  = tmp;
                internal->sdispatches = sdispatches;
        }
        internal->dispatches[internal->ndispatches].sock   = sock;
        internal->dispatches[internal->ndispatches].io     = io;
        internal->dispatches[internal->ndispatches].events = events;
        internal->ndispatches += 1;
        return 0;
}

/*
 * move every registered io to a wsapoll backend. called with the monitor lock
 * held, from the loop thread (add or run). clearing the wakeup hook tells the
 * monitor to go back to a socket pair wakeup.
 */
static int internal_migrate (struct internal *internal, const char *reason)
{
        int rc;
        struct sock *sock;
        struct medusa_io *io;
        struct medusa_poll_backend *fallback;
        struct medusa_monitor_wsapoll_init_options options;

        if (internal->fallback != NULL) {
                return 0;
        }
        medusa_infof("switching to wsapoll backend, reason: %s", reason);

        options.onevent = internal->onevent;
        options.context = internal->context;
        fallback = medusa_monitor_wsapoll_create(&options);
        if (fallback == NULL) {
                medusa_errorf("can not create wsapoll fallback backend");
                return -1;
        }
        fallback->monitor = internal->backend.monitor;

        while (!TAILQ_EMPTY(&internal->live)) {
                sock = TAILQ_FIRST(&internal->live);
                io = sock->io;
                sock_kill(internal, sock);
                if (io == NULL) {
                        continue;
                }
                rc = fallback->add(fallback, io);
                if (rc < 0) {
                        medusa_errorf("can not move io fd: %d to wsapoll fallback", io->fd);
                        fallback->destroy(fallback);
                        return -1;
                }
        }
        internal->fallback = fallback;
        internal->backend.wakeup = NULL;
        internal->ndispatches = 0;

        /* closing the helper cancels every request still in flight */
        if (internal->afd != NULL) {
                CloseHandle(internal->afd);
                internal->afd = NULL;
        }
        dead_drain(internal, IOCP_DRAIN_TIMEOUT_MS);
        return 0;
}

static int internal_add (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int rc;
        SOCKET base;
        struct sock *sock;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (io->fd < 0) {
                goto bail;
        }
        if (internal->fallback != NULL) {
                return internal->fallback->add(internal->fallback, io);
        }
        if (io->poll_backend_data != NULL) {
                return backend->mod(backend, io);
        }
        base = INVALID_SOCKET;
        internal->test.nadds += 1;
        if (internal->test.add != 0 && internal->test.nadds == internal->test.add) {
                medusa_debugf("test: treating fd: %d as layered", io->fd);
                rc = RESOLVE_LAYERED;
        } else {
                rc = socket_resolve((SOCKET) io->fd, &base);
        }
        if (rc == RESOLVE_LAYERED) {
                if (internal_migrate(internal, "layered socket") < 0) {
                        goto bail;
                }
                return internal->fallback->add(internal->fallback, io);
        }
        sock = (struct sock *) malloc(sizeof(struct sock));
        if (sock == NULL) {
                goto bail;
        }
        memset(sock, 0, sizeof(struct sock));
        sock->io      = io;
        sock->base    = base;
        sock->invalid = (rc == RESOLVE_NOTSOCK) ? 1 : 0;
        sock->state   = SOCK_STATE_LIVE;
        TAILQ_INSERT_TAIL(&internal->live, sock, list);
        io->poll_backend_data = sock;
        sock_queue_update(internal, sock);
        return 0;
bail:   return -1;
}

static int internal_mod (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        struct sock *sock;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (internal->fallback != NULL) {
                return internal->fallback->mod(internal->fallback, io);
        }
        sock = (struct sock *) io->poll_backend_data;
        if (sock == NULL) {
                goto bail;
        }
        sock_queue_update(internal, sock);
        return 0;
bail:   return -1;
}

/* may be called from any thread holding the monitor lock, while run() waits */
static int internal_del (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        struct sock *sock;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (io == NULL) {
                goto bail;
        }
        if (internal->fallback != NULL) {
                return internal->fallback->del(internal->fallback, io);
        }
        sock = (struct sock *) io->poll_backend_data;
        if (sock == NULL) {
                goto bail;
        }
        sock_kill(internal, sock);
        return 0;
bail:   return -1;
}

static int internal_wakeup (struct medusa_poll_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return -1;
        }
        if (PostQueuedCompletionStatus(internal->port, 0, IOCP_KEY_WAKEUP, NULL) == FALSE) {
                medusa_errorf("PostQueuedCompletionStatus failed, error: %lu", GetLastError());
                return -1;
        }
        return 0;
}

/* called with the monitor lock held, returns 1 if the backend migrated */
static int internal_submit (struct internal *internal)
{
        NTSTATUS status;
        ULONG events;
        struct sock *sock;
        while (!TAILQ_EMPTY(&internal->updates)) {
                sock = TAILQ_FIRST(&internal->updates);
                sock_dequeue_update(internal, sock);
                if (sock->state != SOCK_STATE_LIVE || sock->io == NULL) {
                        continue;
                }
                if (sock->invalid) {
                        /* same as POLLNVAL from wsapoll, reported once per add/mod
                         * instead of on every run, so an ignored nval can not spin */
                        if (dispatch_push(internal, sock, sock->io, MEDUSA_IO_EVENT_NVAL) < 0) {
                                return -1;
                        }
                        continue;
                }
                events = events_to_afd(medusa_io_get_events_unlocked(sock->io));
                if (sock->pending) {
                        if ((events & ~sock->pending_events) == 0) {
                                /* everything wanted is already watched, extra
                                 * events are filtered out when they complete */
                                continue;
                        }
                        /* re-armed with the new mask once the cancel completes */
                        sock_cancel(internal, sock);
                        continue;
                }
                internal->test.narms += 1;
                if (internal->test.arm != 0 && internal->test.narms == internal->test.arm) {
                        medusa_debugf("test: failing afd poll for fd: %d", sock->io->fd);
                        status = IOCP_STATUS_NOT_SUPPORTED;
                } else {
                        status = sock_arm(internal, sock, events);
                }
                if (status == IOCP_STATUS_SUCCESS) {
                        continue;
                }
                if (status == IOCP_STATUS_INVALID_HANDLE) {
                        sock->invalid = 1;
                        if (dispatch_push(internal, sock, sock->io, MEDUSA_IO_EVENT_NVAL) < 0) {
                                return -1;
                        }
                        continue;
                }
                medusa_debugf("afd poll failed for fd: %d, status: 0x%08lx", sock->io->fd, (unsigned long) status);
                if (internal_migrate(internal, "afd poll request failed") < 0) {
                        return -1;
                }
                return 1;
        }
        return 0;
}

/* called with the monitor lock held */
static int internal_complete (struct internal *internal, ULONG count)
{
        ULONG i;
        ULONG afd;
        NTSTATUS status;
        unsigned int events;
        unsigned int interest;
        struct sock *sock;
        for (i = 0; i < count; i++) {
                if (internal->entries[i].lpCompletionKey == IOCP_KEY_WAKEUP ||
                    internal->entries[i].lpOverlapped == NULL) {
                        continue;
                }
                sock = CONTAINING_RECORD((PIO_STATUS_BLOCK) internal->entries[i].lpOverlapped, struct sock, iosb);
                sock->pending = 0;
                if (sock->state != SOCK_STATE_LIVE || sock->io == NULL) {
                        /* dead, freed by dead_reap() on the next run */
                        continue;
                }
                events = 0;
                status = sock->iosb.Status;
                if (status == IOCP_STATUS_CANCELLED) {
                        /* we cancelled it to change the mask */
                } else if (status == IOCP_STATUS_INVALID_HANDLE) {
                        sock->invalid = 1;
                        events = MEDUSA_IO_EVENT_NVAL;
                } else if (!IOCP_NT_SUCCESS(status)) {
                        events = MEDUSA_IO_EVENT_ERR;
                } else if (sock->poll_info.NumberOfHandles >= 1) {
                        afd = sock->poll_info.Handles[0].Events;
                        if (afd & AFD_POLL_LOCAL_CLOSE) {
                                sock->invalid = 1;
                        } else if (afd & AFD_POLL_CONNECT_FAIL) {
                                socket_sync_connect_error((SOCKET) sock->io->fd);
                        }
                        events = afd_to_events(afd);
                }
                if (sock->invalid == 0) {
                        /* re-armed with the current interest on the next run */
                        sock_queue_update(internal, sock);
                }
                if (events == 0) {
                        continue;
                }
                interest =medusa_io_get_events_unlocked(sock->io);
                events &= (interest & (MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_PRI)) | MEDUSA_IO_EVENT_ALWAYS;
                if (events == 0) {
                        continue;
                }
                if (dispatch_push(internal, sock, sock->io, events) < 0) {
                        return -1;
                }
        }
        return 0;
}

static int internal_run (struct medusa_poll_backend *backend, struct timespec *timespec)
{
        int i;
        int rc;
        BOOL brc;
        DWORD error;
        DWORD timeout;
        ULONG count;
        struct dispatch *dispatch;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                goto bail;
        }
        if (internal->fallback != NULL) {
                return internal->fallback->run(internal->fallback, timespec);
        }

        internal_lock(internal);
        internal->ndispatches = 0;
        dead_reap(internal);
        rc = internal_submit(internal);
        if (rc != 0) {
                internal_unlock(internal);
                if (rc < 0) {
                        goto bail;
                }
                /* migrated, let the monitor set up its socket wakeup first */
                return 0;
        }
        if (timespec == NULL) {
                timeout = INFINITE;
        } else if (timespec->tv_sec < 0 || timespec->tv_nsec < 0) {
                timeout = 0;
        } else {
                unsigned long long ms;
                /* round up, so a sub millisecond timer does not spin */
                ms = (unsigned long long) timespec->tv_sec * 1000ULL + ((unsigned long long) timespec->tv_nsec + 999999ULL) / 1000000ULL;
                timeout = (ms >= (unsigned long long) INFINITE) ? (INFINITE - 1) : (DWORD) ms;
        }
        if (internal->ndispatches > 0) {
                timeout = 0;
        }
        internal_unlock(internal);

        count = 0;
        brc = GetQueuedCompletionStatusEx(internal->port, internal->entries, IOCP_ENTRIES, &count, timeout, FALSE);
        if (brc == FALSE) {
                error = GetLastError();
                count = 0;
                if (error != WAIT_TIMEOUT) {
                        medusa_errorf("GetQueuedCompletionStatusEx failed, error: %lu", error);
                        goto bail;
                }
        }

        internal_lock(internal);
        rc = internal_complete(internal, count);
        medusa_tracef("run: timeout: %lu, completions: %lu, dispatches: %d", (unsigned long) timeout, (unsigned long) count, internal->ndispatches);
        internal_unlock(internal);
        if (rc < 0) {
                goto bail;
        }

        for (i = 0; i < internal->ndispatches; i++) {
                int valid;
                dispatch = &internal->dispatches[i];
                /* an earlier callback, or another thread, may have deleted it */
                internal_lock(internal);
                valid = (dispatch->sock->state == SOCK_STATE_LIVE && dispatch->sock->io == dispatch->io);
                internal_unlock(internal);
                if (!valid) {
                        continue;
                }
                rc = internal->onevent(backend, dispatch->io, dispatch->events, internal->context, NULL);
                if (rc < 0) {
                        medusa_errorf("internal->onevent failed, rc: %d", rc);
                        goto bail;
                }
        }
        rc = internal->ndispatches;
        internal->ndispatches = 0;
        return rc;
bail:   if (internal != NULL) {
                internal->ndispatches = 0;
        }
        return -1;
}

static void internal_destroy (struct medusa_poll_backend *backend)
{
        struct sock *sock;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return;
        }
        if (internal->fallback != NULL) {
                internal->fallback->destroy(internal->fallback);
                internal->fallback = NULL;
        }
        /* the monitor deletes every io before destroying the backend, this only
         * guards against leftovers; their ios are not touched */
        while (!TAILQ_EMPTY(&internal->live)) {
                sock = TAILQ_FIRST(&internal->live);
                sock->io = NULL;
                sock_kill(internal, sock);
        }
        if (internal->afd != NULL) {
                CloseHandle(internal->afd);
                internal->afd = NULL;
        }
        if (internal->port != NULL) {
                dead_drain(internal, IOCP_DRAIN_TIMEOUT_MS);
                CloseHandle(internal->port);
                internal->port = NULL;
        }
        if (internal->dispatches != NULL) {
                free(internal->dispatches);
        }
        if (internal->wsa) {
                WSACleanup();
        }
        free(internal);
}

/*
 * make sure afd polling works for the socket kinds medusa creates before
 * committing to it: no lsp in the way for tcp/udp over ipv4/ipv6, and a real
 * poll request round trips through the port.
 */
static int internal_probe (struct internal *internal)
{
        static const int families[] = { AF_INET, AF_INET6 };
        static const int types[]    = { SOCK_STREAM, SOCK_DGRAM };
        int f;
        int t;
        int rc;
        int tested;
        BOOL brc;
        ULONG i;
        ULONG count;
        SOCKET s;
        SOCKET base;
        NTSTATUS status;
        ULONGLONG start;
        ULONGLONG elapsed;
        struct sock *sock;
        struct sockaddr_in sin;

        tested = 0;
        for (f = 0; f < (int) (sizeof(families) / sizeof(families[0])); f++) {
                for (t = 0; t < (int) (sizeof(types) / sizeof(types[0])); t++) {
                        s = socket(families[f], types[t], 0);
                        if (s == INVALID_SOCKET) {
                                /* protocol not installed, nothing will use it */
                                continue;
                        }
                        rc = socket_resolve(s, &base);
                        closesocket(s);
                        if (rc != RESOLVE_OK) {
                                medusa_debugf("probe: family: %d, type: %d is not pollable with afd", families[f], types[t]);
                                return -1;
                        }
                        tested += 1;
                }
        }
        if (tested == 0) {
                medusa_debugf("probe: no socket could be created");
                return -1;
        }

        s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s == INVALID_SOCKET) {
                return -1;
        }
        memset(&sin, 0, sizeof(sin));
        sin.sin_family      = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sin.sin_port        = 0;
        if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) == SOCKET_ERROR ||
            socket_resolve(s, &base) != RESOLVE_OK) {
                closesocket(s);
                return -1;
        }
        sock = (struct sock *) malloc(sizeof(struct sock));
        if (sock == NULL) {
                closesocket(s);
                return -1;
        }
        memset(sock, 0, sizeof(struct sock));
        sock->base  = base;
        sock->state = SOCK_STATE_DEAD;
        TAILQ_INSERT_TAIL(&internal->dead, sock, list);

        /* a bound udp socket is writable right away */
        status = sock_arm(internal, sock, AFD_POLL_ALWAYS | AFD_POLL_SEND);
        if (status != IOCP_STATUS_SUCCESS) {
                medusa_debugf("probe: afd poll failed, status: 0x%08lx", (unsigned long) status);
                closesocket(s);
                return -1;
        }
        rc = -1;
        start = GetTickCount64();
        while (sock->pending) {
                elapsed = GetTickCount64() - start;
                if (elapsed >= IOCP_PROBE_TIMEOUT_MS) {
                        break;
                }
                brc = GetQueuedCompletionStatusEx(internal->port, internal->entries, IOCP_ENTRIES, &count, (DWORD) (IOCP_PROBE_TIMEOUT_MS - elapsed), FALSE);
                if (brc == FALSE) {
                        break;
                }
                for (i = 0; i < count; i++) {
                        if (internal->entries[i].lpOverlapped != (LPOVERLAPPED) &sock->iosb) {
                                continue;
                        }
                        sock->pending = 0;
                        if (IOCP_NT_SUCCESS(sock->iosb.Status) &&
                            sock->poll_info.NumberOfHandles >= 1 &&
                            (sock->poll_info.Handles[0].Events & AFD_POLL_SEND)) {
                                rc = 0;
                        }
                }
        }
        if (sock->pending) {
                sock_cancel(internal, sock);
        }
        closesocket(s);
        if (rc < 0) {
                medusa_debugf("probe: afd poll did not report the socket writable");
        }
        /* if the request is still pending, destroy waits for it */
        dead_reap(internal);
        return rc;
}

static void internal_test_init (struct internal *internal)
{
        unsigned long n;
        const char *value;
        value = getenv("MEDUSA_POLL_IOCP_TEST_SWITCH");
        if (value == NULL) {
                return;
        }
        if (sscanf(value, "add:%lu", &n) == 1 && n > 0) {
                internal->test.add = n;
        } else if (sscanf(value, "arm:%lu", &n) == 1 && n > 0) {
                internal->test.arm = n;
        } else {
                return;
        }
        medusa_debugf("test: MEDUSA_POLL_IOCP_TEST_SWITCH=%s", value);
}

struct medusa_poll_backend * medusa_monitor_iocp_create (const struct medusa_monitor_iocp_init_options *options)
{
        WSADATA wsadata;
        struct internal *internal;
        internal = NULL;
        if (options == NULL) {
                goto bail;
        }
        if (nt_load() < 0) {
                medusa_debugf("ntdll functions are not available");
                goto bail;
        }
        internal = (struct internal *) malloc(sizeof(struct internal));
        if (internal == NULL) {
                goto bail;
        }
        memset(internal, 0, sizeof(struct internal));
        TAILQ_INIT(&internal->live);
        TAILQ_INIT(&internal->dead);
        TAILQ_INIT(&internal->updates);
        internal->onevent = options->onevent;
        internal->context = options->context;
        internal->backend.name    = "iocp";
        internal->backend.add     = internal_add;
        internal->backend.mod     = internal_mod;
        internal->backend.del     = internal_del;
        internal->backend.run     = internal_run;
        internal->backend.wakeup  = internal_wakeup;
        internal->backend.destroy = internal_destroy;
        /* the probe below needs winsock, the application may not have started it yet */
        if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
                goto bail;
        }
        internal->wsa = 1;
        internal->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
        if (internal->port == NULL) {
                medusa_debugf("CreateIoCompletionPort failed, error: %lu", GetLastError());
                goto bail;
        }
        internal->afd = afd_open(internal->port);
        if (internal->afd == NULL) {
                goto bail;
        }
        if (internal_probe(internal) < 0) {
                goto bail;
        }
        internal_test_init(internal);
        medusa_debugf("using afd poll with iocp");
        return &internal->backend;
bail:   if (internal != NULL) {
                internal_destroy(&internal->backend);
        }
        return NULL;
}
