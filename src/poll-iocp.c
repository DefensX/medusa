
/*
 * readiness polling with IOCTL_AFD_POLL requests completed on a completion
 * port, the way wepoll, mio and c-ares do it.
 *
 * every socket has one poll request at most, it is armed with the current
 * interest before each wait and again after each completion, which keeps the
 * level triggered behavior of the other backends. the kernel writes into the
 * request until its completion is dequeued, so a deleted socket is parked on
 * the dead list and freed only after that, destroy cancels and drains.
 *
 * a socket that a layered service provider wants to see polled can not go to
 * afd directly, it is polled with WSAPoll in the same run instead, and the
 * wait is shortened while there is one.
 *
 * MEDUSA_POLL_IOCP_DISABLE=1 makes create fail, so the monitor falls back to
 * wsapoll. MEDUSA_POLL_IOCP_SLOW=1 polls every socket the layered way.
 */

#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>

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

#define IOCP_STATUS_PENDING             ((NTSTATUS) 0x00000103L)
#define IOCP_STATUS_INVALID_HANDLE      ((NTSTATUS) 0xC0000008L)
#define IOCP_STATUS_CANCELLED           ((NTSTATUS) 0xC0000120L)
#define IOCP_STATUS_NOT_FOUND           ((NTSTATUS) 0xC0000225L)
#define IOCP_NT_SUCCESS(status)         (((NTSTATUS) (status)) >= 0)
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
#define AFD_POLL_ALWAYS                 (AFD_POLL_DISCONNECT | AFD_POLL_ABORT | AFD_POLL_LOCAL_CLOSE | AFD_POLL_CONNECT_FAIL)

#define IOCP_AFD_GROUP_SIZE             32
#define IOCP_ENTRIES                    256
#define IOCP_SLOW_INTERVAL_MS           10
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
        INIT_ONCE once;
        int loaded;
        nt_create_file_t nt_create_file;
        nt_device_io_control_file_t nt_device_io_control_file;
        nt_cancel_io_file_ex_t nt_cancel_io_file_ex;
} g_nt = {
        INIT_ONCE_STATIC_INIT, 0, NULL, NULL, NULL
};

enum {
        SOCK_POLL_IDLE          = 0,
        SOCK_POLL_PENDING       = 1,
        SOCK_POLL_CANCELLED     = 2
};

enum {
        RESOLVE_AFD             = 0,
        RESOLVE_SLOW            = 1
};

TAILQ_HEAD(afds, afd);
struct afd {
        HANDLE handle;
        int count;
        TAILQ_ENTRY(afd) list;
};

TAILQ_HEAD(socks, sock);
struct sock {
        IO_STATUS_BLOCK iosb;
        afd_poll_info_t poll_info;
        struct medusa_io *io;
        struct afd *afd;
        SOCKET socket;
        SOCKET base;
        int status;
        int dead;
        int closed;
        int queued;
        unsigned int events;
        ULONG pending;
        TAILQ_ENTRY(sock) list;
        TAILQ_ENTRY(sock) update;
};

struct dispatch {
        struct sock *sock;
        unsigned int events;
};

struct internal {
        struct medusa_poll_backend backend;
        HANDLE port;
        int wsa;
        int slow;
        struct afds afds;
        struct socks live;
        struct socks slows;
        struct socks dead;
        struct socks updates;
        int nsocks;
        OVERLAPPED_ENTRY entries[IOCP_ENTRIES];
        WSAPOLLFD *pfds;
        struct sock **psocks;
        int spfds;
        struct dispatch *dispatches;
        int ndispatches;
        int sdispatches;
        int (*onevent) (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param);
        void *context;
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

static BOOL CALLBACK nt_load_once (PINIT_ONCE once, PVOID parameter, PVOID *context)
{
        HMODULE ntdll;
        (void) once;
        (void) parameter;
        (void) context;
        ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == NULL) {
                return TRUE;
        }
        g_nt.nt_create_file             = (nt_create_file_t) (void *) GetProcAddress(ntdll, "NtCreateFile");
        g_nt.nt_device_io_control_file  = (nt_device_io_control_file_t) (void *) GetProcAddress(ntdll, "NtDeviceIoControlFile");
        g_nt.nt_cancel_io_file_ex       = (nt_cancel_io_file_ex_t) (void *) GetProcAddress(ntdll, "NtCancelIoFileEx");
        if (g_nt.nt_create_file != NULL &&
            g_nt.nt_device_io_control_file != NULL &&
            g_nt.nt_cancel_io_file_ex != NULL) {
                g_nt.loaded = 1;
        }
        return TRUE;
}

static int nt_load (void)
{
        InitOnceExecuteOnce(&g_nt.once, nt_load_once, NULL, NULL);
        return (g_nt.loaded) ? 0 : -1;
}

static int env_enabled (const char *name)
{
        const char *value;
        value = getenv(name);
        if (value == NULL || value[0] == '\0' || strcmp(value, "0") == 0) {
                return 0;
        }
        return 1;
}

static struct afd * afd_open (struct internal *internal)
{
        NTSTATUS status;
        HANDLE handle;
        IO_STATUS_BLOCK iosb;
        UNICODE_STRING name;
        OBJECT_ATTRIBUTES attributes;
        struct afd *afd;
        static WCHAR path[] = L"\\Device\\Afd\\Medusa";
        name.Length        = sizeof(path) - sizeof(path[0]);
        name.MaximumLength = sizeof(path);
        name.Buffer        = path;
        memset(&attributes, 0, sizeof(attributes));
        attributes.Length     = sizeof(attributes);
        attributes.ObjectName = &name;
        handle = INVALID_HANDLE_VALUE;
        status = g_nt.nt_create_file(&handle, SYNCHRONIZE, &attributes, &iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, IOCP_FILE_OPEN, 0, NULL, 0);
        if (!IOCP_NT_SUCCESS(status)) {
                medusa_errorf("can not open afd, status: 0x%08lx", (unsigned long) status);
                return NULL;
        }
        if (CreateIoCompletionPort(handle, internal->port, 0, 0) == NULL) {
                medusa_errorf("can not associate afd with port, error: %lu", GetLastError());
                CloseHandle(handle);
                return NULL;
        }
        if (!SetFileCompletionNotificationModes(handle, FILE_SKIP_SET_EVENT_ON_HANDLE)) {
                medusa_errorf("can not set afd notification modes, error: %lu", GetLastError());
                CloseHandle(handle);
                return NULL;
        }
        afd = (struct afd *) malloc(sizeof(struct afd));
        if (afd == NULL) {
                CloseHandle(handle);
                return NULL;
        }
        afd->handle = handle;
        afd->count  = 0;
        TAILQ_INSERT_TAIL(&internal->afds, afd, list);
        return afd;
}

static struct afd * afd_acquire (struct internal *internal)
{
        struct afd *afd;
        afd = TAILQ_LAST(&internal->afds, afds);
        if (afd == NULL || afd->count >= IOCP_AFD_GROUP_SIZE) {
                afd = afd_open(internal);
                if (afd == NULL) {
                        return NULL;
                }
        }
        afd->count += 1;
        return afd;
}

static void afd_release (struct internal *internal, struct afd *afd)
{
        afd->count -= 1;
        TAILQ_REMOVE(&internal->afds, afd, list);
        if (afd->count == 0) {
                CloseHandle(afd->handle);
                free(afd);
                return;
        }
        /* acquire takes the last one, it has room now */
        TAILQ_INSERT_TAIL(&internal->afds, afd, list);
}

static int socket_ioctl_handle (SOCKET socket, DWORD ioctl, SOCKET *handle)
{
        int rc;
        DWORD bytes;
        *handle = INVALID_SOCKET;
        bytes = 0;
        rc = WSAIoctl(socket, ioctl, NULL, 0, handle, sizeof(*handle), &bytes, NULL, NULL);
        if (rc == SOCKET_ERROR || *handle == INVALID_SOCKET) {
                return -1;
        }
        return 0;
}

/*
 * afd polls the base socket. a provider that breaks SIO_BASE_HANDLE, or wants
 * to see poll or select, sits above it and would be bypassed, poll it slowly.
 */
static int socket_resolve (SOCKET socket, SOCKET *base)
{
        SOCKET bh;
        SOCKET ph;
        if (socket_ioctl_handle(socket, SIO_BASE_HANDLE, &bh) != 0) {
                if (WSAGetLastError() == WSAENOTSOCK) {
                        return -1;
                }
                return RESOLVE_SLOW;
        }
        if (socket_ioctl_handle(socket, SIO_BSP_HANDLE_POLL, &ph) == 0 && ph != bh) {
                return RESOLVE_SLOW;
        }
        if (socket_ioctl_handle(socket, SIO_BSP_HANDLE_SELECT, &ph) == 0 && ph != bh) {
                return RESOLVE_SLOW;
        }
        *base = bh;
        return RESOLVE_AFD;
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

/* what the wsapoll backend reports for the same socket state */
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
                events |= MEDUSA_IO_EVENT_IN | MEDUSA_IO_EVENT_HUP;
        }
        if (afd & AFD_POLL_ABORT) {
                events |= MEDUSA_IO_EVENT_HUP | MEDUSA_IO_EVENT_ERR;
        }
        if (afd & AFD_POLL_CONNECT_FAIL) {
                events |= MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_ERR;
        }
        return events;
}

static unsigned int wsapoll_to_events (SHORT revents)
{
        unsigned int events;
        events = 0;
        if (revents & POLLRDNORM) {
                events |= MEDUSA_IO_EVENT_IN;
        }
        if (revents & POLLWRNORM) {
                events |= MEDUSA_IO_EVENT_OUT;
        }
        if (revents & POLLRDBAND) {
                events |= MEDUSA_IO_EVENT_PRI;
        }
        if (revents & POLLHUP) {
                events |= MEDUSA_IO_EVENT_HUP;
        }
        if (revents & POLLERR) {
                if (events == 0) {
                        events |= MEDUSA_IO_EVENT_OUT;
                }
                events |= MEDUSA_IO_EVENT_ERR;
        }
        if (revents & POLLNVAL) {
                events |= MEDUSA_IO_EVENT_NVAL;
        }
        return events;
}

/*
 * msafd records why a non blocking connect failed, read back with SO_ERROR,
 * when select or WSAPoll see the failure. afd is asked directly here, so let
 * select see it once.
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

static NTSTATUS sock_arm (struct sock *sock)
{
        NTSTATUS status;
        ULONG events;
        events = events_to_afd(sock->events);
        sock->poll_info.Timeout.QuadPart  = INT64_MAX;
        sock->poll_info.NumberOfHandles   = 1;
        sock->poll_info.Exclusive         = FALSE;
        sock->poll_info.Handles[0].Handle = (HANDLE) sock->base;
        sock->poll_info.Handles[0].Events = events;
        sock->poll_info.Handles[0].Status = 0;
        sock->iosb.Status                 = IOCP_STATUS_PENDING;
        sock->iosb.Information            = 0;
        status = g_nt.nt_device_io_control_file(sock->afd->handle, NULL, NULL, &sock->iosb, &sock->iosb, IOCTL_AFD_POLL, &sock->poll_info, sizeof(sock->poll_info), &sock->poll_info, sizeof(sock->poll_info));
        if (!IOCP_NT_ERROR(status)) {
                /* a completion is queued for anything but an error, also for a synchronous success */
                sock->status  = SOCK_POLL_PENDING;
                sock->pending = events;
        }
        return status;
}

static void sock_cancel (struct sock *sock)
{
        NTSTATUS status;
        IO_STATUS_BLOCK iosb;
        if (sock->status != SOCK_POLL_PENDING) {
                return;
        }
        if (sock->iosb.Status == IOCP_STATUS_PENDING) {
                status = g_nt.nt_cancel_io_file_ex(sock->afd->handle, &sock->iosb, &iosb);
                if (!IOCP_NT_SUCCESS(status) && status != IOCP_STATUS_NOT_FOUND) {
                        medusa_errorf("can not cancel afd poll, status: 0x%08lx", (unsigned long) status);
                }
        }
        sock->status  = SOCK_POLL_CANCELLED;
        sock->pending = 0;
}

/* afd refused the socket, poll it with WSAPoll from now on */
static void sock_make_slow (struct internal *internal, struct sock *sock)
{
        TAILQ_REMOVE(&internal->live, sock, list);
        TAILQ_INSERT_TAIL(&internal->slows, sock, list);
        afd_release(internal, sock->afd);
        sock->afd = NULL;
}

/* the io may be freed right after this returns, the sock is freed by sock_reap */
static void sock_kill (struct internal *internal, struct sock *sock)
{
        if (sock->dead) {
                return;
        }
        if (sock->io != NULL) {
                sock->io->poll_backend_data = NULL;
                sock->io = NULL;
        }
        sock_dequeue_update(internal, sock);
        if (sock->afd != NULL) {
                TAILQ_REMOVE(&internal->live, sock, list);
        } else {
                TAILQ_REMOVE(&internal->slows, sock, list);
        }
        TAILQ_INSERT_TAIL(&internal->dead, sock, list);
        sock->dead = 1;
        internal->nsocks -= 1;
        sock_cancel(sock);
}

static void sock_reap (struct internal *internal)
{
        struct sock *sock;
        struct sock *nsock;
        TAILQ_FOREACH_SAFE(sock, &internal->dead, list, nsock) {
                if (sock->status != SOCK_POLL_IDLE) {
                        continue;
                }
                TAILQ_REMOVE(&internal->dead, sock, list);
                if (sock->afd != NULL) {
                        afd_release(internal, sock->afd);
                }
                free(sock);
        }
}

/*
 * a socket is dispatched once per run at most, so room for every socket is
 * made before anything is dequeued, and nothing taken from the kernel is lost
 */
static int dispatch_reserve (struct internal *internal)
{
        int sdispatches;
        struct dispatch *dispatches;
        if (internal->sdispatches >= internal->nsocks) {
                return 0;
        }
        sdispatches = internal->nsocks + IOCP_ENTRIES;
        dispatches = (struct dispatch *) realloc(internal->dispatches, sizeof(struct dispatch) * sdispatches);
        if (dispatches == NULL) {
                return -ENOMEM;
        }
        internal->dispatches  = dispatches;
        internal->sdispatches = sdispatches;
        return 0;
}

static void dispatch_push (struct internal *internal, struct sock *sock, unsigned int events)
{
        if (internal->ndispatches >= internal->sdispatches) {
                medusa_errorf("dispatch overflow, fd: %d", (int) sock->socket);
                return;
        }
        internal->dispatches[internal->ndispatches].sock   = sock;
        internal->dispatches[internal->ndispatches].events = events;
        internal->ndispatches += 1;
}

static int internal_add (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        int rc;
        SOCKET base;
        unsigned int events;
        struct sock *sock;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL || io == NULL || io->fd < 0) {
                return -EINVAL;
        }
        if (io->poll_backend_data != NULL) {
                return -EEXIST;
        }
        events = medusa_io_get_events_unlocked(io);
        if (events == 0) {
                return -EINVAL;
        }
        sock = (struct sock *) malloc(sizeof(struct sock));
        if (sock == NULL) {
                return -ENOMEM;
        }
        memset(sock, 0, sizeof(struct sock));
        sock->io     = io;
        sock->socket = (SOCKET) io->fd;
        sock->events = events;
        sock->status = SOCK_POLL_IDLE;
        rc = socket_resolve(sock->socket, &base);
        if (rc < 0) {
                medusa_errorf("fd: %d is not a socket", io->fd);
                free(sock);
                return -ENOTSOCK;
        }
        if (rc == RESOLVE_AFD && internal->slow == 0) {
                sock->afd = afd_acquire(internal);
                if (sock->afd == NULL) {
                        medusa_errorf("can not acquire afd, fd: %d is polled with wsapoll", io->fd);
                }
        }
        if (sock->afd != NULL) {
                sock->base = base;
                TAILQ_INSERT_TAIL(&internal->live, sock, list);
                sock_queue_update(internal, sock);
        } else {
                medusa_debugf("fd: %d is polled with wsapoll", io->fd);
                TAILQ_INSERT_TAIL(&internal->slows, sock, list);
        }
        internal->nsocks += 1;
        io->poll_backend_data = sock;
        return 0;
}

static int internal_mod (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        unsigned int events;
        struct sock *sock;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL || io == NULL) {
                return -EINVAL;
        }
        sock = (struct sock *) io->poll_backend_data;
        if (sock == NULL) {
                return -ENOENT;
        }
        events = medusa_io_get_events_unlocked(io);
        if (events == 0) {
                return -EINVAL;
        }
        sock->events = events;
        if (sock->afd != NULL) {
                sock_queue_update(internal, sock);
        }
        return 0;
}

static int internal_del (struct medusa_poll_backend *backend, struct medusa_io *io)
{
        struct sock *sock;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL || io == NULL) {
                return -EINVAL;
        }
        sock = (struct sock *) io->poll_backend_data;
        if (sock == NULL) {
                return -ENOENT;
        }
        sock_kill(internal, sock);
        return 0;
}

static int internal_wakeup (struct medusa_poll_backend *backend)
{
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return -EINVAL;
        }
        if (!PostQueuedCompletionStatus(internal->port, 0, 0, NULL)) {
                medusa_errorf("can not post wakeup, error: %lu", GetLastError());
                return -EIO;
        }
        return 0;
}

/* arms or re-arms every queued socket, a socket that turns out to be closed is reported */
static void internal_update (struct internal *internal)
{
        NTSTATUS status;
        struct sock *sock;
        struct sock *nsock;
        TAILQ_FOREACH_SAFE(sock, &internal->updates, update, nsock) {
                sock_dequeue_update(internal, sock);
                if (sock->dead || sock->closed || sock->afd == NULL) {
                        continue;
                }
                if (sock->status == SOCK_POLL_PENDING) {
                        if ((events_to_afd(sock->events) & ~sock->pending) != 0) {
                                /* the completion of the cancel queues it again */
                                sock_cancel(sock);
                        }
                        continue;
                }
                if (sock->status == SOCK_POLL_CANCELLED) {
                        continue;
                }
                status = sock_arm(sock);
                if (!IOCP_NT_ERROR(status)) {
                        continue;
                }
                if (status == IOCP_STATUS_INVALID_HANDLE) {
                        sock->closed = 1;
                        dispatch_push(internal, sock, MEDUSA_IO_EVENT_NVAL);
                        continue;
                }
                medusa_errorf("afd poll is refused, status: 0x%08lx, fd: %d is polled with wsapoll", (unsigned long) status, (int) sock->socket);
                sock_make_slow(internal, sock);
        }
}

static void internal_complete (struct internal *internal, ULONG count)
{
        ULONG i;
        ULONG afd;
        NTSTATUS status;
        unsigned int events;
        struct sock *sock;
        for (i = 0; i < count; i++) {
                if (internal->entries[i].lpOverlapped == NULL) {
                        continue;
                }
                sock = CONTAINING_RECORD((PIO_STATUS_BLOCK) internal->entries[i].lpOverlapped, struct sock, iosb);
                sock->status  = SOCK_POLL_IDLE;
                sock->pending = 0;
                if (sock->dead) {
                        continue;
                }
                events = 0;
                status = sock->iosb.Status;
                if (status != IOCP_STATUS_CANCELLED && !IOCP_NT_SUCCESS(status)) {
                        events = MEDUSA_IO_EVENT_OUT | MEDUSA_IO_EVENT_ERR;
                } else if (status != IOCP_STATUS_CANCELLED && sock->poll_info.NumberOfHandles >= 1) {
                        afd = sock->poll_info.Handles[0].Events;
                        if (afd & AFD_POLL_LOCAL_CLOSE) {
                                sock->closed = 1;
                                events = MEDUSA_IO_EVENT_NVAL;
                        } else {
                                if (afd & AFD_POLL_CONNECT_FAIL) {
                                        socket_sync_connect_error(sock->socket);
                                }
                                events = afd_to_events(afd);
                        }
                }
                events &= sock->events | MEDUSA_IO_EVENT_ALWAYS;
                sock_queue_update(internal, sock);
                if (events != 0) {
                        dispatch_push(internal, sock, events);
                }
        }
}

/* the sockets polled with wsapoll, returns how many */
static int internal_slow_prepare (struct internal *internal)
{
        int i;
        int npfds;
        struct sock *sock;
        npfds = 0;
        TAILQ_FOREACH(sock, &internal->slows, list) {
                npfds += 1;
        }
        if (npfds > internal->spfds) {
                WSAPOLLFD *pfds;
                struct sock **psocks;
                pfds = (WSAPOLLFD *) realloc(internal->pfds, sizeof(WSAPOLLFD) * npfds);
                if (pfds == NULL) {
                        return -ENOMEM;
                }
                internal->pfds = pfds;
                psocks = (struct sock **) realloc(internal->psocks, sizeof(struct sock *) * npfds);
                if (psocks == NULL) {
                        return -ENOMEM;
                }
                internal->psocks = psocks;
                internal->spfds  = npfds;
        }
        i = 0;
        TAILQ_FOREACH(sock, &internal->slows, list) {
                internal->pfds[i].fd      = sock->socket;
                internal->pfds[i].events  = 0;
                internal->pfds[i].revents = 0;
                if (sock->events & MEDUSA_IO_EVENT_IN) {
                        internal->pfds[i].events |= POLLRDNORM;
                }
                if (sock->events & MEDUSA_IO_EVENT_OUT) {
                        internal->pfds[i].events |= POLLWRNORM;
                }
                if (sock->events & MEDUSA_IO_EVENT_PRI) {
                        internal->pfds[i].events |= POLLRDBAND;
                }
                internal->psocks[i] = sock;
                i += 1;
        }
        return npfds;
}

static void internal_slow_collect (struct internal *internal, int npfds)
{
        int i;
        unsigned int events;
        struct sock *sock;
        for (i = 0; i < npfds; i++) {
                if (internal->pfds[i].revents == 0) {
                        continue;
                }
                sock = internal->psocks[i];
                if (sock->dead) {
                        continue;
                }
                events = wsapoll_to_events(internal->pfds[i].revents);
                events &= sock->events | MEDUSA_IO_EVENT_ALWAYS;
                if (events != 0) {
                        dispatch_push(internal, sock, events);
                }
        }
}

static int internal_run (struct medusa_poll_backend *backend, struct timespec *timespec)
{
        int i;
        int rc;
        int error;
        int npfds;
        int nready;
        int block_slow;
        int ndispatched;
        BOOL brc;
        DWORD timeout;
        ULONG count;
        struct medusa_io *io;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return -EINVAL;
        }
        if (timespec == NULL) {
                timeout = INFINITE;
        } else {
                uint64_t ms;
                ms = (uint64_t) timespec->tv_sec * 1000 + ((uint64_t) timespec->tv_nsec + 999999) / 1000000;
                timeout = (ms >= INFINITE) ? (INFINITE - 1) : (DWORD) ms;
        }

        npfds = 0;
        internal_lock(internal);
        internal->ndispatches = 0;
        rc = dispatch_reserve(internal);
        if (rc == 0) {
                npfds = internal_slow_prepare(internal);
                rc = (npfds < 0) ? npfds : 0;
        }
        if (rc < 0) {
                internal_unlock(internal);
                return rc;
        }
        internal_update(internal);
        /* wsapoll sockets do not complete on the port, wait on them when there is nothing else */
        block_slow = (npfds > 0 && TAILQ_EMPTY(&internal->live));
        if (!TAILQ_EMPTY(&internal->slows) && timeout > IOCP_SLOW_INTERVAL_MS) {
                timeout = IOCP_SLOW_INTERVAL_MS;
        }
        if (internal->ndispatches > 0) {
                timeout = 0;
        }
        internal_unlock(internal);

        error  = 0;
        nready = 0;
        if (block_slow) {
                nready = WSAPoll(internal->pfds, npfds, (INT) timeout);
                timeout = 0;
        }
        count = 0;
        brc = GetQueuedCompletionStatusEx(internal->port, internal->entries, IOCP_ENTRIES, &count, timeout, FALSE);
        if (brc == FALSE) {
                if (GetLastError() != WAIT_TIMEOUT) {
                        medusa_errorf("GetQueuedCompletionStatusEx failed, error: %lu", GetLastError());
                        error = -EIO;
                }
                count = 0;
        }
        if (!block_slow && npfds > 0) {
                nready = WSAPoll(internal->pfds, npfds, 0);
        }
        if (nready == SOCKET_ERROR) {
                medusa_errorf("WSAPoll failed, error: %d", WSAGetLastError());
                error = -EIO;
        }

        internal_lock(internal);
        internal_complete(internal, count);
        if (nready > 0) {
                internal_slow_collect(internal, npfds);
        }
        internal_unlock(internal);

        rc = 0;
        ndispatched = 0;
        for (i = 0; i < internal->ndispatches; i++) {
                internal_lock(internal);
                io = internal->dispatches[i].sock->io;
                internal_unlock(internal);
                if (io == NULL) {
                        continue;
                }
                rc = internal->onevent(backend, io, internal->dispatches[i].events, internal->context, NULL);
                if (rc < 0) {
                        medusa_errorf("internal->onevent failed, rc: %d", rc);
                        break;
                }
                ndispatched += 1;
        }

        internal_lock(internal);
        internal->ndispatches = 0;
        sock_reap(internal);
        internal_unlock(internal);
        if (rc < 0) {
                return rc;
        }
        return (error < 0) ? error : ndispatched;
}

/* waits for the completion of every request the kernel still owns, frees what returned */
static void internal_drain (struct internal *internal, DWORD timeout)
{
        BOOL brc;
        ULONG i;
        ULONG count;
        ULONGLONG start;
        ULONGLONG elapsed;
        int pending;
        struct sock *sock;
        struct sock *nsock;
        start = GetTickCount64();
        while (1) {
                pending = 0;
                TAILQ_FOREACH(sock, &internal->dead, list) {
                        if (sock->status != SOCK_POLL_IDLE) {
                                pending += 1;
                        }
                }
                if (pending == 0) {
                        break;
                }
                elapsed = GetTickCount64() - start;
                if (elapsed >= timeout) {
                        medusa_errorf("%d afd poll requests did not complete, leaking them", pending);
                        break;
                }
                count = 0;
                brc = GetQueuedCompletionStatusEx(internal->port, internal->entries, IOCP_ENTRIES, &count, (DWORD) (timeout - elapsed), FALSE);
                if (brc == FALSE) {
                        continue;
                }
                for (i = 0; i < count; i++) {
                        if (internal->entries[i].lpOverlapped == NULL) {
                                continue;
                        }
                        sock = CONTAINING_RECORD((PIO_STATUS_BLOCK) internal->entries[i].lpOverlapped, struct sock, iosb);
                        sock->status = SOCK_POLL_IDLE;
                }
        }
        TAILQ_FOREACH_SAFE(sock, &internal->dead, list, nsock) {
                TAILQ_REMOVE(&internal->dead, sock, list);
                if (sock->status != SOCK_POLL_IDLE) {
                        /* never free what the kernel may still write into */
                        continue;
                }
                if (sock->afd != NULL) {
                        afd_release(internal, sock->afd);
                }
                free(sock);
        }
}

static void internal_destroy (struct medusa_poll_backend *backend)
{
        struct afd *afd;
        struct sock *sock;
        struct internal *internal = (struct internal *) backend;
        if (internal == NULL) {
                return;
        }
        while ((sock = TAILQ_FIRST(&internal->live)) != NULL) {
                sock->io = NULL;
                sock_kill(internal, sock);
        }
        while ((sock = TAILQ_FIRST(&internal->slows)) != NULL) {
                sock->io = NULL;
                sock_kill(internal, sock);
        }
        if (internal->port != NULL) {
                internal_drain(internal, IOCP_DRAIN_TIMEOUT_MS);
        }
        while ((afd = TAILQ_FIRST(&internal->afds)) != NULL) {
                TAILQ_REMOVE(&internal->afds, afd, list);
                CloseHandle(afd->handle);
                free(afd);
        }
        if (internal->port != NULL) {
                CloseHandle(internal->port);
        }
        free(internal->pfds);
        free(internal->psocks);
        free(internal->dispatches);
        if (internal->wsa) {
                WSACleanup();
        }
        free(internal);
}

/* a writable udp socket must come back through the port, or afd polling does not work here */
static int internal_probe (struct internal *internal)
{
        int rc;
        BOOL brc;
        ULONG count;
        ULONG i;
        SOCKET socket;
        struct sock *sock;
        NTSTATUS status;
        socket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, 0);
        if (socket == INVALID_SOCKET) {
                medusa_errorf("probe can not create socket, error: %d", WSAGetLastError());
                return -1;
        }
        sock = (struct sock *) malloc(sizeof(struct sock));
        if (sock == NULL) {
                closesocket(socket);
                return -1;
        }
        memset(sock, 0, sizeof(struct sock));
        sock->socket = socket;
        sock->events = MEDUSA_IO_EVENT_OUT;
        sock->dead   = 1;
        rc = socket_resolve(socket, &sock->base);
        if (rc != RESOLVE_AFD) {
                /* layered sockets are polled with wsapoll anyway */
                closesocket(socket);
                free(sock);
                return (rc < 0) ? -1 : 0;
        }
        sock->afd = afd_acquire(internal);
        if (sock->afd == NULL) {
                closesocket(socket);
                free(sock);
                return -1;
        }
        TAILQ_INSERT_TAIL(&internal->dead, sock, list);
        status = sock_arm(sock);
        if (IOCP_NT_ERROR(status)) {
                medusa_errorf("probe afd poll failed, status: 0x%08lx", (unsigned long) status);
                closesocket(socket);
                return -1;
        }
        rc = -1;
        count = 0;
        brc = GetQueuedCompletionStatusEx(internal->port, internal->entries, IOCP_ENTRIES, &count, IOCP_PROBE_TIMEOUT_MS, FALSE);
        for (i = 0; brc && i < count; i++) {
                if (internal->entries[i].lpOverlapped == (LPOVERLAPPED) &sock->iosb) {
                        sock->status = SOCK_POLL_IDLE;
                        if (IOCP_NT_SUCCESS(sock->iosb.Status) &&
                            sock->poll_info.NumberOfHandles >= 1 &&
                            (sock->poll_info.Handles[0].Events & AFD_POLL_SEND)) {
                                rc = 0;
                        }
                }
        }
        if (rc != 0) {
                medusa_errorf("probe afd poll did not complete as expected");
        }
        sock_cancel(sock);
        closesocket(socket);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_poll_backend * medusa_monitor_iocp_create (const struct medusa_monitor_iocp_init_options *options)
{
        int rc;
        WSADATA wsadata;
        struct internal *internal;
        internal = NULL;
        if (options == NULL) {
                goto bail;
        }
        if (env_enabled("MEDUSA_POLL_IOCP_DISABLE")) {
                medusa_infof("iocp backend is disabled by MEDUSA_POLL_IOCP_DISABLE");
                goto bail;
        }
        if (nt_load() != 0) {
                medusa_errorf("can not load ntdll functions");
                goto bail;
        }
        internal = (struct internal *) malloc(sizeof(struct internal));
        if (internal == NULL) {
                goto bail;
        }
        memset(internal, 0, sizeof(struct internal));
        TAILQ_INIT(&internal->afds);
        TAILQ_INIT(&internal->live);
        TAILQ_INIT(&internal->slows);
        TAILQ_INIT(&internal->dead);
        TAILQ_INIT(&internal->updates);
        internal->onevent = options->onevent;
        internal->context = options->context;
        internal->slow    = env_enabled("MEDUSA_POLL_IOCP_SLOW");
        rc = WSAStartup(MAKEWORD(2, 2), &wsadata);
        if (rc != 0) {
                medusa_errorf("WSAStartup failed, rc: %d", rc);
                goto bail;
        }
        internal->wsa = 1;
        internal->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
        if (internal->port == NULL) {
                medusa_errorf("can not create completion port, error: %lu", GetLastError());
                goto bail;
        }
        rc = internal_probe(internal);
        if (rc != 0) {
                goto bail;
        }
        internal->backend.name    = "iocp";
        internal->backend.add     = internal_add;
        internal->backend.mod     = internal_mod;
        internal->backend.del     = internal_del;
        internal->backend.run     = internal_run;
        internal->backend.wakeup  = internal_wakeup;
        internal->backend.destroy = internal_destroy;
        return &internal->backend;
bail:   if (internal != NULL) {
                internal_destroy(&internal->backend);
        }
        return NULL;
}
