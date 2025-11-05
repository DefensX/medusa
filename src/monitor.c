
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>

#if defined(__WINDOWS__)
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "queue.h"
#include "pqueue.h"
#include "pipe.h"

#define MEDUSA_DEBUG_NAME "monitor"
#include "debug.h"
#include "error.h"
#include "clock.h"
#include "io.h"
#include "io-private.h"
#include "timer.h"
#include "timer-private.h"
#include "signal.h"
#include "signal-private.h"
#include "condition.h"
#include "condition-private.h"
#include "tcpsocket.h"
#include "tcpsocket-private.h"
#include "udpsocket.h"
#include "udpsocket-private.h"
#include "exec.h"
#include "exec-private.h"
#include "httprequest.h"
#include "httprequest-private.h"
#include "dnsrequest.h"
#include "dnsrequest-private.h"
#include "dnsresolver.h"
#include "dnsresolver-private.h"
#include "websocketclient.h"
#include "websocketclient-private.h"
#include "websocketserver.h"
#include "websocketserver-private.h"
#include "httpserver.h"
#include "httpserver-private.h"
#include "monitor.h"
#include "monitor-private.h"

#include "subject-struct.h"
#include "condition-struct.h"
#include "signal-struct.h"
#include "timer-struct.h"
#include "io-struct.h"

#include "poll-epoll.h"
#include "poll-kqueue.h"
#include "poll-poll.h"
#include "poll-select.h"
#include "poll-backend.h"

#include "signal-sigaction.h"
#include "signal-null.h"
#include "signal-backend.h"

#include "timer-timerfd.h"
#include "timer-monotonic.h"
#include "timer-backend.h"

enum {
        WAKEUP_REASON_NONE,
        WAKEUP_REASON_LOOP_BREAK,
        WAKEUP_REASON_LOOP_CONTINUE,
        WAKEUP_REASON_SUBJECT_ADD,
        WAKEUP_REASON_SUBJECT_MOD,
        WAKEUP_REASON_SUBJECT_DEL,
};

struct medusa_monitor {
        int running;
        unsigned int flags;
        struct medusa_subjects actives;
        struct medusa_subjects changes;
        struct medusa_subjects deletes;
        struct medusa_subjects rogues;
        struct medusa_subjects whole;
        struct {
                struct medusa_poll_backend *backend;
        } poll;
        struct {
                struct medusa_timer_backend *backend;
                struct medusa_pqueue_head *pqueue;
                int fired;
                int dirty;
                int valid;
                struct medusa_io *io;
        } timer;
        struct {
                struct medusa_signal_backend *backend;
                struct medusa_io *io;
        } signal;
        struct {
                struct medusa_conditions signalled;
        } condition;
        struct {
                int fds[2];
                int fired;
                struct medusa_io *io;
        } wakeup;
        struct {
                int (*callback) (struct medusa_monitor *monitor, unsigned int events, void *context, void *param);
                void *context;
        } onevent;
        pthread_mutex_t mutex;
};

static const struct medusa_monitor_init_options g_init_options = {
        .flags  = MEDUSA_MONITOR_FLAG_DEFAULT,
        .poll   = {
                .type   = MEDUSA_MONITOR_POLL_DEFAULT,
                .u      = { }
        },
        .timer  = {
                .type   = MEDUSA_MONITOR_TIMER_DEFAULT,
                .u      = { }
        },
        .signal = {
                .type   = MEDUSA_MONITOR_SIGNAL_DEFAULT,
                .u      = { }
        },
        .onevent = {
                .callback       = NULL,
                .context        = NULL
        }
};

static int monitor_wakeup_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        unsigned int reason;
        struct medusa_monitor *monitor = (struct medusa_monitor *) context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_IN) {
                while (1) {
#if defined(__WINDOWS__)
                        rc = recv(io->fd, (void *) &reason, sizeof(reason), 0);
#else
                        rc = read(io->fd, (void *) &reason, sizeof(reason));
#endif
                        if (rc == 0) {
                                break;
                        } else if (rc < 0) {
#if defined(__WINDOWS__)
                                if (rc == SOCKET_ERROR) {
                                        switch (WSAGetLastError()) {
                                                case WSAEWOULDBLOCK:    errno = EWOULDBLOCK;    break;
                                                case WSATRY_AGAIN:      errno = EAGAIN;         break;
                                                case WSAEINTR:          errno = EINTR;          break;
                                                case WSAECONNRESET:     errno = ECONNRESET;     break;
                                        }
                                }
#endif

                                if (errno == EAGAIN ||
                                    errno == EWOULDBLOCK ||
                                    errno == EINTR) {
                                        break;
                                }
                                goto bail;
                        } else if (rc != sizeof(reason)) {
                                goto bail;
                        }
                }
                medusa_monitor_lock(monitor);
                monitor->wakeup.fired = 0;
                medusa_monitor_unlock(monitor);
        }
        return 0;
bail:   return -1;
}

static int monitor_timer_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        uint64_t value;
        struct medusa_monitor *monitor = context;
        (void) param;
        if (events & MEDUSA_IO_EVENT_IN) {
                while (1) {
                        rc = read(io->fd, &value, sizeof(value));
                        if (rc == 0) {
                                break;
                        } else if (rc < 0) {
                                if (errno == EAGAIN ||
                                    errno == EWOULDBLOCK ||
                                    errno == EINTR) {
                                        break;
                                }
                                goto bail;
                        } else if (rc != sizeof(value)) {
                                goto bail;
                        }
                }
                medusa_monitor_lock(monitor);
                monitor->timer.fired = 1;
                medusa_monitor_unlock(monitor);
        }
        return 0;
bail:   return -1;
}

static int monitor_signal_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        struct medusa_monitor *monitor = context;
        (void) io;
        (void) param;
        if (events & MEDUSA_IO_EVENT_IN) {
                rc = monitor->signal.backend->run(monitor->signal.backend);
                if (rc < 0) {
                        return rc;
                }
        }
        return 0;
}

static int monitor_signal (struct medusa_monitor *monitor, unsigned int reason)
{
        int rc;
        if (monitor->wakeup.fired == 0) {
#if defined(__WINDOWS__)
                rc = send(monitor->wakeup.fds[1], (void *) &reason, sizeof(reason), 0);
#else
                rc = write(monitor->wakeup.fds[1], (void *) &reason, sizeof(reason));
#endif
                if (rc != sizeof(reason)) {
                        goto bail;
                }
                monitor->wakeup.fired = 1;
        }
        if (reason == WAKEUP_REASON_LOOP_BREAK) {
                monitor->running = 0;
        } else if (reason == WAKEUP_REASON_LOOP_CONTINUE) {
                monitor->running = 1;
        }
        return 0;
bail:   return -1;
}

static int monitor_timer_subject_compare (void *a, void *b)
{
        struct medusa_timer *ta = a;
        struct medusa_timer *tb = b;
        return medusa_timespec_compare(&ta->_timespec, &tb->_timespec, >);
}

static void monitor_timer_subject_set_position (void *entry, unsigned int position)
{
        struct medusa_timer *timer = entry;
        timer->_position = position;
}

static unsigned int monitor_timer_subject_get_position (void *entry)
{
        struct medusa_timer *timer = entry;
        return timer->_position;
}

static int monitor_subject_onevent (struct medusa_monitor *monitor, struct medusa_subject *subject, unsigned int events, void *param)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                medusa_errorf("monitor: %p is invalid", monitor);
                rc = -EINVAL;
                goto bail;
        }
        if (MEDUSA_IS_ERR_OR_NULL(subject)) {
                medusa_errorf("subject: %p is invalid", subject);
                rc = -EINVAL;
                goto bail;
        }
        switch (medusa_subject_get_type(subject)) {
                case MEDUSA_SUBJECT_TYPE_IO:
                        rc = medusa_io_onevent_unlocked((struct medusa_io *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_TIMER:
                        rc = medusa_timer_onevent_unlocked((struct medusa_timer *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_SIGNAL:
                        rc = medusa_signal_onevent_unlocked((struct medusa_signal *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_CONDITION:
                        rc = medusa_condition_onevent_unlocked((struct medusa_condition *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_TCPSOCKET:
                        rc = medusa_tcpsocket_onevent_unlocked((struct medusa_tcpsocket *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_UDPSOCKET:
                        rc = medusa_udpsocket_onevent_unlocked((struct medusa_udpsocket *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_EXEC:
#if defined(MEDUSA_EXEC_ENABLE) && (MEDUSA_EXEC_ENABLE == 1)
                        rc = medusa_exec_onevent_unlocked((struct medusa_exec *) subject, events, param);
#else
                        rc = -ENOTSUP;
#endif
                        break;
                case MEDUSA_SUBJECT_TYPE_HTTPREQUEST:
                        rc = medusa_httprequest_onevent_unlocked((struct medusa_httprequest *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_DNSREQUEST:
                        rc = medusa_dnsrequest_onevent_unlocked((struct medusa_dnsrequest *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_DNSRESOLVER:
                        rc = medusa_dnsresolver_onevent_unlocked((struct medusa_dnsresolver *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_DNSRESOLVER_LOOKUP:
                        rc = medusa_dnsresolver_lookup_onevent_unlocked((struct medusa_dnsresolver_lookup *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_WEBSOCKETCLIENT:
                        rc = medusa_websocketclient_onevent_unlocked((struct medusa_websocketclient *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER:
                        rc = medusa_websocketserver_onevent_unlocked((struct medusa_websocketserver *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER_CLIENT:
                        rc = medusa_websocketserver_client_onevent_unlocked((struct medusa_websocketserver_client *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_HTTPSERVER:
                        rc = medusa_httpserver_onevent_unlocked((struct medusa_httpserver *) subject, events, param);
                        break;
                case MEDUSA_SUBJECT_TYPE_HTTPSERVER_CLIENT:
                        rc = medusa_httpserver_client_onevent_unlocked((struct medusa_httpserver_client *) subject, events, param);
                        break;
                default:
                        rc = -ENOENT;
        }
        if (rc < 0) {
                struct medusa_monitor_event_error medusa_monitor_event_error;
                if (monitor->onevent.callback == NULL) {
                        goto bail;
                }
                medusa_monitor_event_error.type = MEDUSA_MONITOR_ERROR_TYPE_SUBJECT_ONEVENT;
                medusa_monitor_event_error.u.subject_onevent.subject_type = medusa_subject_get_type(subject);
                rc = monitor->onevent.callback(monitor, MEDUSA_MONITOR_EVENT_ERROR, monitor->onevent.context, &medusa_monitor_event_error);
                if (rc < 0) {
                        goto bail;
                }
        }
        return 0;
bail:   return rc;
}

static int monitor_poll_io_onevent (struct medusa_poll_backend *backend, struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        struct medusa_monitor *monitor = (struct medusa_monitor *) context;
        (void) backend;
        medusa_monitor_lock(monitor);
        rc = monitor_subject_onevent(monitor, &io->subject, events, param);
        medusa_monitor_unlock(monitor);
        return rc;
}

static int monitor_process_deletes (struct medusa_monitor *monitor)
{
        int rc;
        struct medusa_subject *subject;
        struct medusa_subject *nsubject;
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPSERVER_CLIENT) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_HTTPSERVER_CLIENT_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPSERVER) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_HTTPSERVER_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETCLIENT) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER_CLIENT) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_WEBSOCKETSERVER_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSRESOLVER_LOOKUP) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSRESOLVER) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_DNSRESOLVER_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSREQUEST) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_DNSREQUEST_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPREQUEST) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_HTTPREQUEST_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_EXEC) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_EXEC_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_TCPSOCKET) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_TCPSOCKET_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_UDPSOCKET) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_UDPSOCKET_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_IO) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                rc = monitor->poll.backend->del(monitor->poll.backend, (struct medusa_io *) subject);
                                if (rc != 0) {
                                        goto bail;
                                }
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_IO_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_TIMER) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                rc = medusa_pqueue_del(monitor->timer.pqueue, (struct medusa_timer *) subject);
                                if (rc != 0) {
                                        goto bail;
                                }
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                                monitor->timer.dirty = 1;
                        }
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_TIMER_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_SIGNAL) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                rc = monitor->signal.backend->del(monitor->signal.backend, (struct medusa_signal *) subject);
                                if (rc != 0) {
                                        goto bail;
                                }
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_SIGNAL_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_CONDITION) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                TAILQ_REMOVE(&monitor->condition.signalled, (struct medusa_condition *) subject, _signalled);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_CONDITION_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSRESOLVER_LOOKUP) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSREQUEST) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        rc = monitor_subject_onevent(monitor, subject, MEDUSA_DNSREQUEST_EVENT_DESTROY, NULL);
                        if (rc < 0) {
                                goto bail;
                        }
                }
        }
        if (!TAILQ_EMPTY(&monitor->deletes)) {
                rc = monitor_signal(monitor, WAKEUP_REASON_SUBJECT_DEL);
                if (rc < 0) {
                        goto bail;
                }
        }
        return 0;
bail:   return -EIO;
}

static int monitor_process_changes (struct medusa_monitor *monitor)
{
        int rc;
        struct timespec now;
        struct medusa_subject *subject;
        struct medusa_subject *nsubject;
        rc = medusa_clock_monotonic(&now);
        if (rc < 0) {
                goto bail;
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->changes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_IO) {
                        struct medusa_io *io;
                        io = (struct medusa_io *) subject;
                        if (!medusa_io_is_valid_unlocked(io)) {
                                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                        rc = monitor->poll.backend->del(monitor->poll.backend, io);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                }
                                TAILQ_REMOVE(&monitor->changes, subject, hook);
                                TAILQ_INSERT_TAIL(&monitor->rogues, subject, hook);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                                subject->flags |= MEDUSA_SUBJECT_FLAG_ROGUE;
                        } else {
                                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                        rc = monitor->poll.backend->mod(monitor->poll.backend, io);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                } else {
                                        rc = monitor->poll.backend->add(monitor->poll.backend, io);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                }
                                TAILQ_REMOVE(&monitor->changes, subject, hook);
                                TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                                subject->flags |= MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_TIMER) {
                        struct medusa_timer *timer;
                        timer = (struct medusa_timer *) subject;
                        if (!medusa_timer_is_valid_unlocked(timer)) {
                                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                        rc = medusa_pqueue_del(monitor->timer.pqueue, timer);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                        monitor->timer.dirty = 1;
                                }
                                medusa_timespec_clear(&timer->_timespec);
                                TAILQ_REMOVE(&monitor->changes, subject, hook);
                                TAILQ_INSERT_TAIL(&monitor->rogues, subject, hook);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                                subject->flags |= MEDUSA_SUBJECT_FLAG_ROGUE;
                        } else {
                                struct timespec _timespec;
                                _timespec = timer->_timespec;
                                rc = medusa_timer_update_timespec_unlocked(timer, &now);
                                if (rc < 0) {
                                        goto bail;
                                }
                                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                        rc = medusa_pqueue_mod(monitor->timer.pqueue, timer, medusa_timespec_compare(&_timespec, &timer->_timespec, >));
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                } else {
                                        rc = medusa_pqueue_add(monitor->timer.pqueue, subject);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                }
                                TAILQ_REMOVE(&monitor->changes, subject, hook);
                                TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                                subject->flags |= MEDUSA_SUBJECT_FLAG_HEAP;
                                monitor->timer.dirty = 1;
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_SIGNAL) {
                        struct medusa_signal *signal;
                        signal = (struct medusa_signal *) subject;
                        if (!medusa_signal_is_valid_unlocked(signal)) {
                                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                        rc = monitor->signal.backend->del(monitor->signal.backend, signal);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                }
                                TAILQ_REMOVE(&monitor->changes, subject, hook);
                                TAILQ_INSERT_TAIL(&monitor->rogues, subject, hook);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                                subject->flags |= MEDUSA_SUBJECT_FLAG_ROGUE;
                        } else {
                                if (!(subject->flags & MEDUSA_SUBJECT_FLAG_HEAP)) {
                                        rc = monitor->signal.backend->add(monitor->signal.backend, signal);
                                        if (rc != 0) {
                                                goto bail;
                                        }
                                }
                                TAILQ_REMOVE(&monitor->changes, subject, hook);
                                TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                                subject->flags |= MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_CONDITION) {
                        struct medusa_condition *condition;
                        condition = (struct medusa_condition *) subject;
                        if (!medusa_condition_is_valid_unlocked(condition)) {
                                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                        TAILQ_REMOVE(&monitor->condition.signalled, condition, _signalled);
                                }
                                TAILQ_REMOVE(&monitor->changes, subject, hook);
                                TAILQ_INSERT_TAIL(&monitor->rogues, subject, hook);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                                subject->flags |= MEDUSA_SUBJECT_FLAG_ROGUE;
                        } else {
                                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                        TAILQ_REMOVE(&monitor->condition.signalled, condition, _signalled);
                                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                                }
                                TAILQ_REMOVE(&monitor->changes, subject, hook);
                                TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                                if (medusa_condition_get_signalled_unlocked(condition) > 0) {
                                        TAILQ_INSERT_TAIL(&monitor->condition.signalled, condition, _signalled);
                                        subject->flags |= MEDUSA_SUBJECT_FLAG_HEAP;
                                }
                        }
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_TCPSOCKET) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_UDPSOCKET) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_EXEC) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPREQUEST) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSRESOLVER_LOOKUP) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSRESOLVER) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSREQUEST) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPSERVER) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPSERVER_CLIENT) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETCLIENT) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER_CLIENT) {
                        TAILQ_REMOVE(&monitor->changes, subject, hook);
                        TAILQ_INSERT_TAIL(&monitor->actives, subject, hook);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_MOD;
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
                }
        }
        return 0;
bail:   return rc;
}

static int monitor_setup_timer (struct medusa_monitor *monitor, struct timespec *remaining)
{
        int rc;
        struct medusa_timer *timer;
        if (monitor->timer.dirty != 0) {
                timer = medusa_pqueue_peek(monitor->timer.pqueue);
                rc = monitor->timer.backend->set(monitor->timer.backend, (timer) ? &timer->_timespec : NULL);
                if (rc != 0) {
                        goto bail;
                }
                monitor->timer.dirty = 0;
                monitor->timer.valid = (timer) ? 1 : 0;
        }
        if (monitor->timer.backend->fd == NULL && monitor->timer.valid == 1) {
                rc = monitor->timer.backend->get(monitor->timer.backend, remaining);
                if (rc < 0) {
                        goto bail;
                }
        } else {
                medusa_timespec_clear(remaining);
        }
        return 0;
bail:   return -1;
}

static __attribute__ ((__unused__))  int monitor_hit_timer (void *context, void *a)
{
        int rc;
        struct medusa_timer *timer = a;
        struct medusa_monitor *monitor = context;
        (void) monitor;
        rc = monitor_subject_onevent(monitor, &timer->subject, MEDUSA_TIMER_EVENT_TIMEOUT, NULL);
        if (rc != 0) {
                goto bail;
        }
        rc = medusa_monitor_mod_unlocked(&timer->subject);
        if (rc != 0) {
                goto bail;
        }
        return 0;
bail:   return -1;
}

static int monitor_check_timer (struct medusa_monitor *monitor)
{
        int rc;
        struct timespec now;
        struct timespec rem;
        if (monitor->timer.backend->fd == NULL && monitor->timer.valid == 1) {
                rc = monitor->timer.backend->get(monitor->timer.backend, &rem);
                if (rc < 0) {
                        goto bail;
                }
                if (rem.tv_sec <= 0 ||
                    rem.tv_nsec <= 0) {
                        monitor->timer.fired = 1;
                }
        }
        if (monitor->timer.fired != 0) {
                rc = medusa_clock_monotonic(&now);
                if (rc < 0) {
                        goto bail;
                }
#if 0
                struct medusa_timer *timer;
                while (1) {
                        timer = medusa_pqueue_peek(monitor->timer.pqueue);
                        if (timer == NULL) {
                                break;
                        }
                        if (medusa_timespec_compare(&timer->_timespec, &now, >)) {
                                break;
                        }
                        timer = medusa_pqueue_pop(monitor->timer.pqueue);
                        if (timer == NULL) {
                                break;
                        }
                        timer->subject.flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        monitor->timer.dirty = 1;
                        rc = monitor_subject_onevent(monitor, &timer->subject, MEDUSA_TIMER_EVENT_TIMEOUT);
                        if (rc != 0) {
                                goto bail;
                        }
                        rc = medusa_monitor_mod_unlocked(&timer->subject);
                        if (rc != 0) {
                                goto bail;
                        }
                }
#else
                struct medusa_timer tk;
                memset(&tk, 0, sizeof(struct medusa_timer));
                tk._timespec = now;
                rc = medusa_pqueue_search(monitor->timer.pqueue, &tk, monitor_hit_timer, monitor);
                if (rc != 0) {
                        goto bail;
                }
#endif
                monitor->timer.fired = 0;
        }
        return 0;
bail:   return -1;
}

static int monitor_check_condition (struct medusa_monitor *monitor)
{
        int rc;
        struct medusa_condition *condition;
        struct medusa_condition *ncondition;
        TAILQ_FOREACH_SAFE(condition, &monitor->condition.signalled, _signalled, ncondition) {
                TAILQ_REMOVE(&monitor->condition.signalled, condition, _signalled);
                condition->subject.flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                rc = monitor_subject_onevent(monitor, &condition->subject, MEDUSA_CONDITION_EVENT_SIGNAL, NULL);
                if (rc != 0) {
                        goto bail;
                }
                rc = medusa_monitor_mod_unlocked(&condition->subject);
                if (rc != 0) {
                        goto bail;
                }
        }
        return 0;
bail:   return -1;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_lock (struct medusa_monitor *monitor)
{
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        if (monitor->flags & MEDUSA_MONITOR_FLAG_THREAD_SAFE) {
                pthread_mutex_lock(&monitor->mutex);
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_unlock (struct medusa_monitor *monitor)
{
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        if (monitor->flags & MEDUSA_MONITOR_FLAG_THREAD_SAFE) {
                pthread_mutex_unlock(&monitor->mutex);
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_add_unlocked (struct medusa_monitor *monitor, struct medusa_subject *subject)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(subject)) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(subject->monitor)) {
                return -EALREADY;
        }
        if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_SIGNAL) {
                if (MEDUSA_IS_ERR_OR_NULL(monitor->signal.backend)) {
                        return -ENOENT;
                }
        }
        TAILQ_INSERT_TAIL(&monitor->changes, subject, hook);
        TAILQ_INSERT_TAIL(&monitor->whole, subject, list);
        subject->monitor = monitor;
        subject->flags |= MEDUSA_SUBJECT_FLAG_MOD;
        rc = monitor_signal(monitor, WAKEUP_REASON_SUBJECT_ADD);
        if (rc < 0) {
                return rc;
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_add (struct medusa_monitor *monitor, struct medusa_subject *subject)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(subject)) {
                return -EINVAL;
        }
        if (!MEDUSA_IS_ERR_OR_NULL(subject->monitor)) {
                return -EALREADY;
        }
        medusa_monitor_lock(monitor);
        rc = medusa_monitor_add_unlocked(monitor, subject);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_mod_unlocked (struct medusa_subject *subject)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(subject)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(subject->monitor)) {
                return -EINVAL;
        }
        if (subject->flags & MEDUSA_SUBJECT_FLAG_DEL) {
        } else if (subject->flags & MEDUSA_SUBJECT_FLAG_MOD) {
        } else if (subject->flags & MEDUSA_SUBJECT_FLAG_ROGUE) {
                TAILQ_REMOVE(&subject->monitor->rogues, subject, hook);
                TAILQ_INSERT_TAIL(&subject->monitor->changes, subject, hook);
                subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
        } else {
                TAILQ_REMOVE(&subject->monitor->actives, subject, hook);
                TAILQ_INSERT_TAIL(&subject->monitor->changes, subject, hook);
        }
        subject->flags |= MEDUSA_SUBJECT_FLAG_MOD;
        rc = 0;
        if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_IO) {
                struct medusa_io *io;
                io = (struct medusa_io *) subject;
                if (!medusa_io_is_valid_unlocked(io) &&
                    (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP)) {
                        rc = subject->monitor->poll.backend->del(subject->monitor->poll.backend, io);
                        if (rc < 0) {
                                goto out;
                        }
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                }
#if 1
        } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_TIMER) {
                struct medusa_timer *timer;
                timer = (struct medusa_timer *) subject;
                if (!medusa_timer_is_valid_unlocked(timer) &&
                    (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP)) {
                        rc = medusa_pqueue_del(subject->monitor->timer.pqueue, timer);
                        if (rc < 0) {
                                goto out;
                        }
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        subject->monitor->timer.dirty = 1;
                }
#endif
        } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_SIGNAL) {
                struct medusa_signal *signal;
                signal = (struct medusa_signal *) subject;
                if (!medusa_signal_is_valid_unlocked(signal) &&
                    (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP)) {
                        rc = subject->monitor->signal.backend->del(subject->monitor->signal.backend, signal);
                        if (rc < 0) {
                                goto out;
                        }
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                }
        } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_CONDITION) {
                struct medusa_condition *condition;
                condition = (struct medusa_condition *) subject;
                if (!medusa_condition_is_valid_unlocked(condition) &&
                    (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP)) {
                        TAILQ_REMOVE(&subject->monitor->condition.signalled, condition, _signalled);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                }
        }
        rc = monitor_signal(subject->monitor, WAKEUP_REASON_SUBJECT_MOD);
        if (rc < 0) {
                return rc;
        }
out:    return rc;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_mod (struct medusa_subject *subject)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(subject)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(subject->monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(subject->monitor);
        rc = medusa_monitor_mod_unlocked(subject);
        medusa_monitor_unlock(subject->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_del_unlocked (struct medusa_subject *subject)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(subject)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(subject->monitor)) {
                return -EINVAL;
        }
        if (subject->flags & MEDUSA_SUBJECT_FLAG_DEL) {
        } else if (subject->flags & MEDUSA_SUBJECT_FLAG_MOD) {
                TAILQ_REMOVE(&subject->monitor->changes, subject, hook);
                TAILQ_INSERT_TAIL(&subject->monitor->deletes, subject, hook);
        } else if (subject->flags & MEDUSA_SUBJECT_FLAG_ROGUE) {
                TAILQ_REMOVE(&subject->monitor->rogues, subject, hook);
                TAILQ_INSERT_TAIL(&subject->monitor->deletes, subject, hook);
                subject->flags &= ~MEDUSA_SUBJECT_FLAG_ROGUE;
        } else {
                TAILQ_REMOVE(&subject->monitor->actives, subject, hook);
                TAILQ_INSERT_TAIL(&subject->monitor->deletes, subject, hook);
        }
        subject->flags |= MEDUSA_SUBJECT_FLAG_DEL;
        rc = 0;
        if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_IO) {
                struct medusa_io *io;
                io = (struct medusa_io *) subject;
                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                        rc = subject->monitor->poll.backend->del(subject->monitor->poll.backend, io);
                        if (rc < 0) {
                                goto out;
                        }
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                }
        } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_TIMER) {
                struct medusa_timer *timer;
                timer = (struct medusa_timer *) subject;
                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                        rc = medusa_pqueue_del(subject->monitor->timer.pqueue, timer);
                        if (rc < 0) {
                                goto out;
                        }
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        subject->monitor->timer.dirty = 1;
                }
        } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_SIGNAL) {
                struct medusa_signal *signal;
                signal = (struct medusa_signal *) subject;
                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                        rc = subject->monitor->signal.backend->del(subject->monitor->signal.backend, signal);
                        if (rc < 0) {
                                goto out;
                        }
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                }
        } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_CONDITION) {
                struct medusa_condition *condition;
                condition = (struct medusa_condition *) subject;
                if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                        TAILQ_REMOVE(&subject->monitor->condition.signalled, condition, _signalled);
                        subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                }
        }
        rc = monitor_signal(subject->monitor, WAKEUP_REASON_SUBJECT_DEL);
        if (rc < 0) {
                return rc;
        }
out:    return rc;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_del (struct medusa_subject *subject)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(subject)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(subject->monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(subject->monitor);
        rc = medusa_monitor_del_unlocked(subject);
        medusa_monitor_unlock(subject->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_init_options_default (struct medusa_monitor_init_options *options)
{
        if (options == NULL) {
                goto bail;
        }
        memcpy(options, &g_init_options, sizeof(struct medusa_monitor_init_options));
        return 0;
bail:   return -EIO;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_monitor_create_with_options (const struct medusa_monitor_init_options *options)
{
        int rc;
        struct medusa_monitor *monitor;
        monitor = NULL;
        if (options == NULL) {
                options = &g_init_options;
        }
        monitor = (struct medusa_monitor *) malloc(sizeof(struct medusa_monitor));
        if (monitor == NULL) {
                medusa_errorf("can not allocate memory");
                goto bail;
        }
        memset(monitor, 0, sizeof(struct medusa_monitor));
        TAILQ_INIT(&monitor->actives);
        TAILQ_INIT(&monitor->changes);
        TAILQ_INIT(&monitor->deletes);
        TAILQ_INIT(&monitor->rogues);
        TAILQ_INIT(&monitor->whole);
        monitor->flags = options->flags;
        if (monitor->flags & MEDUSA_MONITOR_FLAG_THREAD_SAFE) {
                pthread_mutex_init(&monitor->mutex, NULL);
        }
        monitor->running = 1;
        monitor->wakeup.fds[0] = -1;
        monitor->wakeup.fds[1] = -1;
        if (options->poll.type == MEDUSA_MONITOR_POLL_DEFAULT) {
                do {
#if defined(MEDUSA_POLL_EPOLL_ENABLE) && (MEDUSA_POLL_EPOLL_ENABLE == 1)
                        struct medusa_monitor_epoll_init_options epoll_init_options;
                        epoll_init_options.onevent = monitor_poll_io_onevent;
                        epoll_init_options.context = monitor;
                        monitor->poll.backend = medusa_monitor_epoll_create(&epoll_init_options);
                        if (monitor->poll.backend != NULL) {
                                break;
                        }
#endif
#if defined(MEDUSA_POLL_KQUEUE_ENABLE) && (MEDUSA_POLL_KQUEUE_ENABLE == 1)
                        struct medusa_monitor_kqueue_init_options kqueue_init_options;
                        kqueue_init_options.onevent = monitor_poll_io_onevent;
                        kqueue_init_options.context = monitor;
                        monitor->poll.backend = medusa_monitor_kqueue_create(&kqueue_init_options);
                        if (monitor->poll.backend != NULL) {
                                break;
                        }
#endif
#if defined(MEDUSA_POLL_POLL_ENABLE) && (MEDUSA_POLL_POLL_ENABLE == 1)
                        struct medusa_monitor_poll_init_options poll_init_options;
                        poll_init_options.onevent = monitor_poll_io_onevent;
                        poll_init_options.context = monitor;
                        monitor->poll.backend = medusa_monitor_poll_create(&poll_init_options);
                        if (monitor->poll.backend != NULL) {
                                break;
                        }
#endif
#if defined(MEDUSA_POLL_SELECT_ENABLE) && (MEDUSA_POLL_SELECT_ENABLE == 1)
                        struct medusa_monitor_select_init_options select_init_options;
                        select_init_options.onevent = monitor_poll_io_onevent;
                        select_init_options.context = monitor;
                        monitor->poll.backend = medusa_monitor_select_create(&select_init_options);
                        if (monitor->poll.backend != NULL) {
                                break;
                        }
#endif
                } while (0);
#if defined(MEDUSA_POLL_EPOLL_ENABLE) && (MEDUSA_POLL_EPOLL_ENABLE == 1)
        } else if (options->poll.type == MEDUSA_MONITOR_POLL_EPOLL) {
                struct medusa_monitor_epoll_init_options epoll_init_options;
                epoll_init_options.onevent = monitor_poll_io_onevent;
                epoll_init_options.context = monitor;
                monitor->poll.backend = medusa_monitor_epoll_create(&epoll_init_options);
#endif
#if defined(MEDUSA_POLL_KQUEUE_ENABLE) && (MEDUSA_POLL_KQUEUE_ENABLE == 1)
        } else if (options->poll.type == MEDUSA_MONITOR_POLL_KQUEUE) {
                struct medusa_monitor_kqueue_init_options kqueue_init_options;
                kqueue_init_options.onevent = monitor_poll_io_onevent;
                kqueue_init_options.context = monitor;
                monitor->poll.backend = medusa_monitor_kqueue_create(&kqueue_init_options);
#endif
#if defined(MEDUSA_POLL_POLL_ENABLE) && (MEDUSA_POLL_POLL_ENABLE == 1)
        } else if (options->poll.type == MEDUSA_MONITOR_POLL_POLL) {
                struct medusa_monitor_poll_init_options poll_init_options;
                poll_init_options.onevent = monitor_poll_io_onevent;
                poll_init_options.context = monitor;
                monitor->poll.backend = medusa_monitor_poll_create(&poll_init_options);
#endif
#if defined(MEDUSA_POLL_SELECT_ENABLE) && (MEDUSA_POLL_SELECT_ENABLE == 1)
        } else if (options->poll.type == MEDUSA_MONITOR_POLL_SELECT) {
                struct medusa_monitor_select_init_options select_init_options;
                select_init_options.onevent = monitor_poll_io_onevent;
                select_init_options.context = monitor;
                monitor->poll.backend = medusa_monitor_select_create(&select_init_options);
        } else {
#endif
                medusa_errorf("invalid poll type: %d", options->poll.type);
                goto bail;
        }
        if (monitor->poll.backend == NULL) {
                medusa_errorf("can not create poll backend");
                goto bail;
        }
        monitor->poll.backend->monitor = monitor;
        if (options->timer.type == MEDUSA_MONITOR_TIMER_DEFAULT) {
                do {
#if defined(MEDUSA_TIMER_TIMERFD_ENABLE) && (MEDUSA_TIMER_TIMERFD_ENABLE == 1)
                        monitor->timer.backend = medusa_timer_timerfd_create(NULL);
                        if (monitor->timer.backend != NULL) {
                                break;
                        }
#endif
#if defined(MEDUSA_TIMER_MONOTONIC_ENABLE) && (MEDUSA_TIMER_MONOTONIC_ENABLE == 1)
                        monitor->timer.backend = medusa_timer_monotonic_create(NULL);
                        if (monitor->timer.backend != NULL) {
                                break;
                        }
#endif
                } while (0);
#if defined(MEDUSA_TIMER_TIMERFD_ENABLE) && (MEDUSA_TIMER_TIMERFD_ENABLE == 1)
        } else if (options->timer.type == MEDUSA_MONITOR_TIMER_TIMERFD) {
                monitor->timer.backend = medusa_timer_timerfd_create(NULL);
#endif
#if defined(MEDUSA_TIMER_MONOTONIC_ENABLE) && (MEDUSA_TIMER_MONOTONIC_ENABLE == 1)
        } else if (options->timer.type == MEDUSA_MONITOR_TIMER_MONOTONIC) {
                monitor->timer.backend = medusa_timer_monotonic_create(NULL);
#endif
        } else {
                medusa_errorf("invalid timer type: %d", options->timer.type);
                goto bail;
        }
        if (monitor->timer.backend == NULL) {
                medusa_errorf("can not create timer backend");
                goto bail;
        }
        monitor->timer.backend->monitor = monitor;
        monitor->timer.pqueue = medusa_pqueue_create(0, 64, monitor_timer_subject_compare, monitor_timer_subject_set_position, monitor_timer_subject_get_position);
        if (monitor->timer.pqueue == NULL) {
                medusa_errorf("can not create timer pqueue");
                goto bail;
        }
        if (options->signal.type == MEDUSA_MONITOR_SIGNAL_DEFAULT) {
                do {
#if defined(MEDUSA_SIGNAL_SIGACTION_ENABLE) && (MEDUSA_SIGNAL_SIGACTION_ENABLE == 1)
                        monitor->signal.backend = medusa_signal_sigaction_create(NULL);
                        if (monitor->signal.backend != NULL) {
                                break;
                        }
#endif
#if defined(MEDUSA_SIGNAL_NULL_ENABLE) && (MEDUSA_SIGNAL_NULL_ENABLE == 1)
                        monitor->signal.backend = medusa_signal_null_create(NULL);
                        if (monitor->signal.backend != NULL) {
                                break;
                        }
#endif
                } while (0);
#if defined(MEDUSA_SIGNAL_SIGACTION_ENABLE) && (MEDUSA_SIGNAL_SIGACTION_ENABLE == 1)
        } else if (options->signal.type == MEDUSA_MONITOR_SIGNAL_SIGACTION) {
                monitor->signal.backend = medusa_signal_sigaction_create(NULL);
#endif
#if defined(MEDUSA_SIGNAL_NULL_ENABLE) && (MEDUSA_SIGNAL_NULL_ENABLE == 1)
        } else if (options->signal.type == MEDUSA_MONITOR_SIGNAL_NULL) {
                monitor->signal.backend = medusa_signal_null_create(NULL);
#endif
        } else {
                medusa_errorf("invalid signal type: %d", options->signal.type);
                goto bail;
        }
        if (MEDUSA_IS_ERR_OR_NULL(monitor->signal.backend)) {
                if (!MEDUSA_IS_ERR(monitor->signal.backend) ||
                    (MEDUSA_PTR_ERR(monitor->signal.backend) != -EALREADY)) {
                        medusa_errorf("can not create signal backend");
                        goto bail;
                }
        }
        TAILQ_INIT(&monitor->condition.signalled);
        rc = medusa_pipe2(monitor->wakeup.fds, MEDUSA_PIPE_FLAG_NONBLOCK);
        if (rc != 0) {
                medusa_errorf("can not create wakeup fds");
                goto bail;
        }
        monitor->wakeup.io = medusa_io_create(monitor, monitor->wakeup.fds[0], monitor_wakeup_io_onevent, monitor);
        if (MEDUSA_IS_ERR_OR_NULL(monitor->wakeup.io)) {
                medusa_errorf("can not create wakeup io");
                goto bail;
        }
        rc = medusa_io_set_events(monitor->wakeup.io, MEDUSA_IO_EVENT_IN);
        if (rc < 0) {
                medusa_errorf("can not setup wakeup io");
                goto bail;
        }
        rc = medusa_io_set_enabled(monitor->wakeup.io, 1);
        if (rc < 0) {
                medusa_errorf("can not enable wakeup io");
                goto bail;
        }
        if (monitor->timer.backend->fd != NULL) {
                monitor->timer.io = medusa_io_create(monitor, monitor->timer.backend->fd(monitor->timer.backend), monitor_timer_io_onevent, monitor);
                if (MEDUSA_IS_ERR_OR_NULL(monitor->timer.io)) {
                        medusa_errorf("can not create timer io");
                        goto bail;
                }
                rc = medusa_io_set_events(monitor->timer.io, MEDUSA_IO_EVENT_IN);
                if (rc < 0) {
                        medusa_errorf("can not setup timer io");
                        goto bail;
                }
                rc = medusa_io_set_enabled(monitor->timer.io, 1);
                if (rc < 0) {
                        medusa_errorf("can not enable timer io");
                        goto bail;
                }
        }
        if (!MEDUSA_IS_ERR_OR_NULL(monitor->signal.backend)) {
                monitor->signal.backend->monitor = monitor;
                if (monitor->signal.backend->fd != NULL) {
                        monitor->signal.io = medusa_io_create(monitor, monitor->signal.backend->fd(monitor->signal.backend), monitor_signal_io_onevent, monitor);
                        if (MEDUSA_IS_ERR_OR_NULL(monitor->signal.io)) {
                                medusa_errorf("can not create signal io");
                                goto bail;
                        }
                        rc = medusa_io_set_events(monitor->signal.io, MEDUSA_IO_EVENT_IN);
                        if (rc < 0) {
                                medusa_errorf("can not setup signal io");
                                goto bail;
                        }
                        rc = medusa_io_set_enabled(monitor->signal.io, 1);
                        if (rc < 0) {
                                medusa_errorf("can not enable signal io");
                                goto bail;
                        }
                }
        }
        monitor->onevent.callback = options->onevent.callback;
        monitor->onevent.context  = options->onevent.context;
        return monitor;
bail:   if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return NULL;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_monitor_create (void)
{
        int rc;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options monitor_init_options;
        rc = medusa_monitor_init_options_default(&monitor_init_options);
        if (rc != 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        monitor = medusa_monitor_create_with_options(&monitor_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return monitor;
        }
        return monitor;
}

__attribute__ ((visibility ("default"))) void medusa_monitor_destroy (struct medusa_monitor *monitor)
{
        struct medusa_subject *subject;
        struct medusa_subject *nsubject;
        if (monitor == NULL) {
                return;
        }
        medusa_monitor_lock(monitor);
        if (!MEDUSA_IS_ERR_OR_NULL(monitor->wakeup.io)) {
                medusa_io_destroy_unlocked(monitor->wakeup.io);
        }
        if (!MEDUSA_IS_ERR_OR_NULL(monitor->timer.io)) {
                medusa_io_destroy_unlocked(monitor->timer.io);
        }
        if (!MEDUSA_IS_ERR_OR_NULL(monitor->signal.io)) {
                medusa_io_destroy_unlocked(monitor->signal.io);
        }
        while (!TAILQ_EMPTY(&monitor->rogues)) {
                subject = TAILQ_FIRST(&monitor->rogues);
                medusa_monitor_del_unlocked(subject);
        }
        while (!TAILQ_EMPTY(&monitor->changes)) {
                subject = TAILQ_FIRST(&monitor->changes);
                medusa_monitor_del_unlocked(subject);
        }
        while (!TAILQ_EMPTY(&monitor->actives)) {
                subject = TAILQ_FIRST(&monitor->actives);
                medusa_monitor_del_unlocked(subject);
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPSERVER_CLIENT) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_httpserver_client_onevent_unlocked((struct medusa_httpserver_client *) subject, MEDUSA_HTTPSERVER_CLIENT_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPSERVER) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_httpserver_onevent_unlocked((struct medusa_httpserver *) subject, MEDUSA_HTTPSERVER_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETCLIENT) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_websocketclient_onevent_unlocked((struct medusa_websocketclient *) subject, MEDUSA_WEBSOCKETCLIENT_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER_CLIENT) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_websocketserver_client_onevent_unlocked((struct medusa_websocketserver_client *) subject, MEDUSA_WEBSOCKETSERVER_CLIENT_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_WEBSOCKETSERVER) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_websocketserver_onevent_unlocked((struct medusa_websocketserver *) subject, MEDUSA_WEBSOCKETSERVER_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_HTTPREQUEST) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_httprequest_onevent_unlocked((struct medusa_httprequest *) subject, MEDUSA_HTTPREQUEST_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSRESOLVER_LOOKUP) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_dnsresolver_lookup_onevent_unlocked((struct medusa_dnsresolver_lookup *) subject, MEDUSA_DNSRESOLVER_LOOKUP_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSRESOLVER) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_dnsresolver_onevent_unlocked((struct medusa_dnsresolver *) subject, MEDUSA_DNSRESOLVER_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_DNSREQUEST) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_dnsrequest_onevent_unlocked((struct medusa_dnsrequest *) subject, MEDUSA_DNSREQUEST_EVENT_DESTROY, NULL);
                }
        }
#if defined(MEDUSA_EXEC_ENABLE) && (MEDUSA_EXEC_ENABLE == 1)
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_EXEC) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_exec_onevent_unlocked((struct medusa_exec *) subject, MEDUSA_EXEC_EVENT_DESTROY, NULL);
                }
        }
#endif
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_TCPSOCKET) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_tcpsocket_onevent_unlocked((struct medusa_tcpsocket *) subject, MEDUSA_TCPSOCKET_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_UDPSOCKET) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        medusa_udpsocket_onevent_unlocked((struct medusa_udpsocket *) subject, MEDUSA_UDPSOCKET_EVENT_DESTROY, NULL);
                }
        }
        TAILQ_FOREACH_SAFE(subject, &monitor->deletes, hook, nsubject) {
                if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_IO) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                monitor->poll.backend->del(monitor->poll.backend, (struct medusa_io *) subject);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                        medusa_io_onevent_unlocked((struct medusa_io *) subject, MEDUSA_IO_EVENT_DESTROY, NULL);
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_TIMER) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                medusa_pqueue_del(monitor->timer.pqueue, (struct medusa_timer *) subject);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                        medusa_timer_onevent_unlocked((struct medusa_timer *) subject, MEDUSA_TIMER_EVENT_DESTROY, NULL);
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_SIGNAL) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                monitor->signal.backend->del(monitor->signal.backend, (struct medusa_signal *) subject);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                        medusa_signal_onevent_unlocked((struct medusa_signal *) subject, MEDUSA_SIGNAL_EVENT_DESTROY, NULL);
                } else if (medusa_subject_get_type(subject) == MEDUSA_SUBJECT_TYPE_CONDITION) {
                        TAILQ_REMOVE(&monitor->deletes, subject, hook);
                        TAILQ_REMOVE(&monitor->whole, subject, list);
                        if (subject->flags & MEDUSA_SUBJECT_FLAG_HEAP) {
                                TAILQ_REMOVE(&subject->monitor->condition.signalled, (struct medusa_condition *) subject, _signalled);
                                subject->flags &= ~MEDUSA_SUBJECT_FLAG_HEAP;
                        }
                        medusa_condition_onevent_unlocked((struct medusa_condition *) subject, MEDUSA_CONDITION_EVENT_DESTROY, NULL);
                }
        }
        if (monitor->poll.backend != NULL) {
                monitor->poll.backend->destroy(monitor->poll.backend);
        }
        if (!MEDUSA_IS_ERR_OR_NULL(monitor->signal.backend)) {
                monitor->signal.backend->destroy(monitor->signal.backend);
        }
        if (monitor->timer.backend != NULL) {
                monitor->timer.backend->destroy(monitor->timer.backend);
        }
        if (monitor->wakeup.fds[0] >= 0) {
                close(monitor->wakeup.fds[0]);
        }
        if (monitor->wakeup.fds[1] >= 0) {
                close(monitor->wakeup.fds[1]);
        }
        if (monitor->timer.pqueue != NULL) {
                medusa_pqueue_destroy(monitor->timer.pqueue);
        }
        medusa_monitor_unlock(monitor);
        if (monitor->flags & MEDUSA_MONITOR_FLAG_THREAD_SAFE) {
             pthread_mutex_destroy(&monitor->mutex);
        }
        free(monitor);
}

__attribute__ ((visibility ("default"))) int medusa_monitor_get_running (struct medusa_monitor *monitor)
{
        int running;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(monitor);
        running = monitor->running;
        medusa_monitor_unlock(monitor);
        return running;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_break (struct medusa_monitor *monitor)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(monitor);
        rc = monitor_signal(monitor, WAKEUP_REASON_LOOP_BREAK);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_continue (struct medusa_monitor *monitor)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(monitor);
        rc = monitor_signal(monitor, WAKEUP_REASON_LOOP_CONTINUE);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_run_timeout (struct medusa_monitor *monitor, double timeout)
{
        int rc;
        struct timespec *timespec;
        struct timespec _timespec;
        struct timespec remaining;

        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }

        medusa_monitor_lock(monitor);

        rc = monitor_process_deletes(monitor);
        if (rc < 0) {
                goto bail;
        }
        rc = monitor_process_changes(monitor);
        if (rc < 0) {
                goto bail;
        }

        rc = monitor_check_condition(monitor);
        if (rc < 0) {
                goto bail;
        }
        rc = monitor_process_changes(monitor);
        if (rc < 0) {
                goto bail;
        }

        rc = monitor_setup_timer(monitor, &remaining);
        if (rc < 0) {
                goto bail;
        }

        if (monitor->wakeup.fired != 0) {
                timeout = 0;
        }
        if (timeout < 0) {
                timespec = NULL;
        } else if (timeout == 0) {
                timespec = &_timespec;
                timespec->tv_sec  = 0;
                timespec->tv_nsec = 0;
        } else {
                timespec = &_timespec;
                timespec->tv_sec  = timeout;
                timespec->tv_nsec = (timeout - timespec->tv_sec) * 1e9;
        }

        medusa_monitor_unlock(monitor);

        if (monitor->timer.backend->fd == NULL && monitor->timer.valid == 1) {
                if (timespec == NULL ||
                    medusa_timespec_compare(&remaining, timespec, <)) {
                        timespec = &remaining;
                }
        }

        if (timespec) {
                if (timespec->tv_sec < 0 || timespec->tv_nsec < 0) {
                        timespec->tv_sec = 0;
                        timespec->tv_nsec = 0;
                }
        }

        rc = monitor->poll.backend->run(monitor->poll.backend, timespec);

        medusa_monitor_lock(monitor);

        if (rc < 0) {
                goto bail;
        }

        rc = monitor_check_timer(monitor);
        if (rc < 0) {
                goto bail;
        }

        rc = monitor->running;

bail:   medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_monitor_run_once (struct medusa_monitor *monitor)
{
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        return medusa_monitor_run_timeout(monitor, -1);
}

__attribute__ ((visibility ("default"))) int medusa_monitor_run (struct medusa_monitor *monitor)
{
        int rc;

        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }

        while (1) {
                rc = medusa_monitor_run_once(monitor);
                if (rc == 0) {
                        return rc;
                } else if (rc < 0) {
                        return rc;
                }
        }

        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_subject * medusa_monitor_get_first_subject_unlocked (struct medusa_monitor *monitor)
{
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return TAILQ_FIRST(&monitor->whole);
}

__attribute__ ((visibility ("default"))) struct medusa_subject * medusa_monitor_get_next_subject_unlocked (struct medusa_subject *subject)
{
        if (MEDUSA_IS_ERR_OR_NULL(subject)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return TAILQ_NEXT(subject, list);
}

__attribute__ ((visibility ("default"))) const char * medusa_monitor_event_string (unsigned int event)
{
        if (event == MEDUSA_MONITOR_EVENT_ERROR)        return "MEDUSA_MONITOR_EVENT_ERROR";
        if (event == MEDUSA_MONITOR_EVENT_DESTROY)      return "MEDUSA_MONITOR_EVENT_DESTROY";
        return "MEDUSA_MONITOR_EVENT_UNKNOWN";
}
