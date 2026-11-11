#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#if defined(__LINUX__)

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "medusa/error.h"
#include "medusa/io.h"
#include "medusa/monitor.h"

#include "medusa/queue.h"
#include "medusa/monitor-private.h"
#include "medusa/subject-struct.h"

/*
 * the wakeup channel of the monitor can be stalled, dropped or reset by
 * something that is not us, like a wfp driver on windows. send/write would
 * block, the peer is gone, the connection is reset or a wakeup is accepted
 * and never arrives. none of these may fail the caller, may stop the loop,
 * or may make a break/continue lost. the channel is recreated by the loop and
 * the next wakeup is delivered through the new one.
 *
 * the channel is a pipe, or a udp socket that is connected to itself, the
 * test finds out which one and injects the faults that fit it.
 */

static int g_udp;

/* descriptors that are taken to make creating anything fail, and the limit they are taken under */
static int g_hogs[256];
static int g_nhogs = -1;
static struct rlimit g_rlimit;

static double cpu (void)
{
        struct timespec ts;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int hogs_take (void)
{
        struct rlimit limit;
        if (getrlimit(RLIMIT_NOFILE, &g_rlimit) != 0) {
                return -1;
        }
        limit = g_rlimit;
        if (limit.rlim_cur > (rlim_t) (sizeof(g_hogs) / sizeof(g_hogs[0]))) {
                limit.rlim_cur = sizeof(g_hogs) / sizeof(g_hogs[0]);
        }
        if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
                return -1;
        }
        g_nhogs = 0;
        while (g_nhogs < (int) (sizeof(g_hogs) / sizeof(g_hogs[0]))) {
                g_hogs[g_nhogs] = dup(0);
                if (g_hogs[g_nhogs] < 0) {
                        return 0;
                }
                g_nhogs++;
        }
        return -1;
}

static void hogs_release (void)
{
        if (g_nhogs < 0) {
                return;
        }
        while (g_nhogs > 0) {
                close(g_hogs[--g_nhogs]);
        }
        setrlimit(RLIMIT_NOFILE, &g_rlimit);
        g_nhogs = -1;
}

static const unsigned int g_polls[] = {
        MEDUSA_MONITOR_POLL_DEFAULT,
        MEDUSA_MONITOR_POLL_EPOLL,
        MEDUSA_MONITOR_POLL_POLL,
        MEDUSA_MONITOR_POLL_SELECT,
};

static double elapsed (const struct timespec *start)
{
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (now.tv_sec - start->tv_sec) + (now.tv_nsec - start->tv_nsec) / 1e9;
}

static int count_fds (void)
{
        int count;
        DIR *dir;
        count = 0;
        dir = opendir("/proc/self/fd");
        if (dir == NULL) {
                return -1;
        }
        while (readdir(dir) != NULL) {
                count++;
        }
        closedir(dir);
        return count;
}

static void fill (int fd)
{
        char buffer[4096];
        memset(buffer, 0, sizeof(buffer));
        while (send(fd, buffer, sizeof(buffer), MSG_DONTWAIT) > 0) {
        }
}

static void fill_pipe (int fd)
{
        char buffer[4096];
        memset(buffer, 0, sizeof(buffer));
        while (write(fd, buffer, sizeof(buffer)) > 0) {
        }
}

/*
 * a channel is told from its replacement by what it is, not by the number of its
 * descriptor, which is given out again as soon as the old one is closed
 */
static unsigned long wakeup_id (int fd)
{
        struct stat st;
        if (fd < 0 || fstat(fd, &st) != 0) {
                return 0;
        }
        return (unsigned long) st.st_ino;
}

/* the descriptor that the monitor polls for wakeups */
static int find_wakeup_fd (struct medusa_monitor *monitor)
{
        int fd;
        int type;
        socklen_t length;
        struct stat st;
        struct medusa_subject *subject;
        fd = -1;
        for (subject = medusa_monitor_get_first_subject_unlocked(monitor); subject; subject = medusa_monitor_get_next_subject_unlocked(subject)) {
                int sfd;
                if (medusa_subject_get_type(subject) != MEDUSA_SUBJECT_TYPE_IO) {
                        continue;
                }
                sfd = medusa_io_get_fd((struct medusa_io *) subject);
                if (fstat(sfd, &st) != 0) {
                        continue;
                }
                if (S_ISFIFO(st.st_mode)) {
                        g_udp = 0;
                        fd = sfd;
                        break;
                }
                length = sizeof(type);
                if (S_ISSOCK(st.st_mode) &&
                    getsockopt(sfd, SOL_SOCKET, SO_TYPE, &type, &length) == 0 &&
                    type == SOCK_DGRAM) {
                        g_udp = 1;
                        fd = sfd;
                        break;
                }
        }
        return fd;
}

/* the write end of a pipe is the other descriptor that is the same pipe */
static int find_pipe_wfd (int rfd)
{
        int fd;
        char path[64];
        char link[64];
        char other[64];
        ssize_t length;
        snprintf(path, sizeof(path), "/proc/self/fd/%d", rfd);
        length = readlink(path, link, sizeof(link) - 1);
        if (length < 0) {
                return -1;
        }
        link[length] = '\0';
        for (fd = 0; fd < 1024; fd++) {
                if (fd == rfd) {
                        continue;
                }
                snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
                length = readlink(path, other, sizeof(other) - 1);
                if (length < 0) {
                        continue;
                }
                other[length] = '\0';
                if (strcmp(link, other) != 0) {
                        continue;
                }
                if ((fcntl(fd, F_GETFL) & O_ACCMODE) == O_WRONLY) {
                        return fd;
                }
        }
        return -1;
}

static int udp_bound (struct sockaddr_in *address)
{
        int fd;
        socklen_t length;
        fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (fd < 0) {
                return -1;
        }
        memset(address, 0, sizeof(*address));
        address->sin_family      = AF_INET;
        address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        length = sizeof(*address);
        if (bind(fd, (struct sockaddr *) address, sizeof(*address)) != 0 ||
            getsockname(fd, (struct sockaddr *) address, &length) != 0) {
                close(fd);
                return -1;
        }
        return fd;
}

/* a connected tcp socket whose peer is gone with a reset, reading it fails with ECONNRESET */
static int tcp_reset (void)
{
        int ls;
        int c;
        int s;
        socklen_t length;
        struct linger linger;
        struct sockaddr_in address;
        c = -1;
        s = -1;
        ls = socket(AF_INET, SOCK_STREAM, 0);
        if (ls < 0) {
                return -1;
        }
        memset(&address, 0, sizeof(address));
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        length = sizeof(address);
        if (bind(ls, (struct sockaddr *) &address, sizeof(address)) != 0 ||
            listen(ls, 1) != 0 ||
            getsockname(ls, (struct sockaddr *) &address, &length) != 0) {
                goto bail;
        }
        c = socket(AF_INET, SOCK_STREAM, 0);
        if (c < 0 ||
            connect(c, (struct sockaddr *) &address, sizeof(address)) != 0) {
                goto bail;
        }
        s = accept(ls, NULL, NULL);
        if (s < 0) {
                goto bail;
        }
        linger.l_onoff  = 1;
        linger.l_linger = 0;
        setsockopt(s, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));
        close(s);
        close(ls);
        usleep(50000);
        fcntl(c, F_SETFL, fcntl(c, F_GETFL) | O_NONBLOCK);
        return c;
bail:   if (s >= 0) {
                close(s);
        }
        if (c >= 0) {
                close(c);
        }
        close(ls);
        return -1;
}

struct breaker {
        struct medusa_monitor *monitor;
};

static void * breaker_routine (void *arg)
{
        struct breaker *breaker = arg;
        usleep(200000);
        medusa_monitor_break(breaker->monitor);
        return NULL;
}

/* a wakeup from another thread must interrupt a loop that sleeps for long */
static int check_wakeup (struct medusa_monitor *monitor)
{
        int rc;
        pthread_t thread;
        struct breaker breaker;
        struct timespec start;

        rc = medusa_monitor_continue(monitor);
        if (rc != 0) {
                fprintf(stderr, "medusa_monitor_continue failed, rc: %d\n", rc);
                return -1;
        }
        rc = medusa_monitor_run_timeout(monitor, 0);
        if (rc != 1) {
                fprintf(stderr, "medusa_monitor_run_timeout is not running, rc: %d\n", rc);
                return -1;
        }
        breaker.monitor = monitor;
        pthread_create(&thread, NULL, breaker_routine, &breaker);
        clock_gettime(CLOCK_MONOTONIC, &start);
        rc = medusa_monitor_run_timeout(monitor, 10.0);
        pthread_join(thread, NULL);
        if (rc != 0) {
                fprintf(stderr, "medusa_monitor_run_timeout did not break, rc: %d\n", rc);
                return -1;
        }
        if (elapsed(&start) > 2.0) {
                fprintf(stderr, "medusa_monitor_run_timeout did not wake up, elapsed: %.3f\n", elapsed(&start));
                return -1;
        }
        return 0;
}

/*
 * the break is requested while the channel is faulty, it must be applied and
 * the loop must not sleep for it, the channel must be replaced afterwards.
 */
static int check_break (struct medusa_monitor *monitor, unsigned long id, int recreate)
{
        int rc;
        struct timespec start;

        rc = medusa_monitor_break(monitor);
        if (rc != 0) {
                fprintf(stderr, "medusa_monitor_break failed, rc: %d\n", rc);
                return -1;
        }
        if (medusa_monitor_get_running(monitor) != 0) {
                fprintf(stderr, "medusa_monitor_break is not applied\n");
                return -1;
        }
        clock_gettime(CLOCK_MONOTONIC, &start);
        rc = medusa_monitor_run_timeout(monitor, 10.0);
        if (rc != 0) {
                fprintf(stderr, "medusa_monitor_run_timeout did not break, rc: %d\n", rc);
                return -1;
        }
        if (elapsed(&start) > 2.0) {
                fprintf(stderr, "medusa_monitor_run_timeout slept for the dropped wakeup, elapsed: %.3f\n", elapsed(&start));
                return -1;
        }
        if (recreate && wakeup_id(find_wakeup_fd(monitor)) == id) {
                fprintf(stderr, "wakeup channel is not recreated\n");
                return -1;
        }
        return check_wakeup(monitor);
}

enum {
        FAULT_NONE,
        FAULT_FULL,
        FAULT_WRITE_WOULDBLOCK,
        FAULT_WRITE_BROKEN,
        FAULT_WRITE_UNCONNECTED,
        FAULT_READ_EOF,
        FAULT_READ_STALL,
        FAULT_UNREPLACEABLE,
        FAULT_READ_RESET,
        FAULT_MAX
};

static const char * fault_string (int fault)
{
        switch (fault) {
                case FAULT_NONE:                return "no fault, wakeup from another thread";
                case FAULT_FULL:                return "channel is full";
                case FAULT_WRITE_WOULDBLOCK:    return "write would block";
                case FAULT_WRITE_BROKEN:        return "write on a closed channel";
                case FAULT_WRITE_UNCONNECTED:   return "write on a udp socket that is not connected";
                case FAULT_READ_EOF:            return "read end is closed";
                case FAULT_READ_STALL:          return "wakeup is written but never readable";
                case FAULT_UNREPLACEABLE:       return "channel is closed and can not be replaced";
                case FAULT_READ_RESET:          return "connection is reset (econnreset) on the read end";
        }
        return "unknown";
}

/* faults that are not injected in place need the descriptor of the poll backend to stay */
static int fault_applies (unsigned int poll, int fault)
{
        int epoll;
        epoll = (poll == MEDUSA_MONITOR_POLL_DEFAULT || poll == MEDUSA_MONITOR_POLL_EPOLL);
        switch (fault) {
                case FAULT_UNREPLACEABLE:
                        return (g_udp) ? 1 : !epoll;
                case FAULT_READ_RESET:
                        return !epoll;
                case FAULT_WRITE_UNCONNECTED:
                        return g_udp;
                case FAULT_WRITE_WOULDBLOCK:
                        /* the descriptor of a self connected socket is replaced, epoll does not follow */
                        return (g_udp) ? !epoll : 1;
                case FAULT_READ_EOF:
                        return (g_udp) ? 0 : !epoll;
                case FAULT_READ_STALL:
                        return (g_udp) ? 1 : !epoll;
        }
        return 1;
}

static int test_fault (unsigned int poll, int fault)
{
        int i;
        int rc;
        int fd;
        int wfd;
        unsigned long id;
        int keep;
        int extra;
        int sink;
        int fds[2];
        int recreate;
        struct sockaddr_in address;
        struct medusa_monitor *monitor;
        struct medusa_monitor_init_options options;

        keep     = -1;
        extra    = -1;
        sink     = -1;
        fds[0]   = -1;
        fds[1]   = -1;
        monitor  = NULL;
        recreate = 1;

        fprintf(stderr, "  fault: %s\n", fault_string(fault));

        medusa_monitor_init_options_default(&options);
        options.poll.type   = poll;
        options.signal.type = MEDUSA_MONITOR_SIGNAL_NULL;

        monitor = medusa_monitor_create_with_options(&options);
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                fprintf(stderr, "medusa_monitor_create failed\n");
                monitor = NULL;
                goto bail;
        }
        rc = medusa_monitor_run_timeout(monitor, 0);
        if (rc < 0) {
                fprintf(stderr, "medusa_monitor_run_timeout failed, rc: %d\n", rc);
                goto bail;
        }
        fd = find_wakeup_fd(monitor);
        id = wakeup_id(fd);
        if (fd < 0) {
                fprintf(stderr, "can not find the wakeup descriptor\n");
                goto bail;
        }
        wfd = (g_udp) ? fd : find_pipe_wfd(fd);
        if (wfd < 0) {
                fprintf(stderr, "can not find the write descriptor\n");
                goto bail;
        }

        if (!fault_applies(poll, fault)) {
                fprintf(stderr, "    not applicable\n");
                medusa_monitor_destroy(monitor);
                return 0;
        }

        switch (fault) {
                case FAULT_NONE:
                        recreate = 0;
                        break;
                case FAULT_FULL:
                        if (g_udp) {
                                /* what does not fit is dropped without an error, what is queued wakes the loop */
                                i = 1;
                                setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &i, sizeof(i));
                                extra = dup(fd);
                                for (i = 0; i < 200; i++) {
                                        send(extra, &i, sizeof(i), MSG_DONTWAIT);
                                }
                                recreate = 0;
                        } else {
                                extra = dup(wfd);
                                fill_pipe(extra);
                        }
                        break;
                case FAULT_WRITE_WOULDBLOCK:
                        keep = dup(wfd);
                        if (keep < 0) {
                                goto bail;
                        }
                        if (g_udp) {
                                if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) != 0) {
                                        goto bail;
                                }
                                fill(fds[0]);
                                dup2(fds[0], wfd);
                                close(fds[0]);
                                fds[0] = -1;
                        } else {
                                if (pipe2(fds, O_NONBLOCK) != 0) {
                                        goto bail;
                                }
                                fill_pipe(fds[1]);
                                dup2(fds[1], wfd);
                                close(fds[1]);
                                fds[1] = -1;
                        }
                        break;
                case FAULT_WRITE_BROKEN:
                        if (g_udp) {
                                shutdown(fd, SHUT_RDWR);
                        } else {
                                keep = dup(wfd);
                                if (keep < 0 || pipe2(fds, O_NONBLOCK) != 0) {
                                        goto bail;
                                }
                                close(fds[0]);
                                fds[0] = -1;
                                dup2(fds[1], wfd);
                                close(fds[1]);
                                fds[1] = -1;
                        }
                        break;
                case FAULT_WRITE_UNCONNECTED:
                        memset(&address, 0, sizeof(address));
                        address.sin_family = AF_UNSPEC;
                        connect(fd, (struct sockaddr *) &address, sizeof(address));
                        break;
                case FAULT_READ_EOF:
                        if (pipe2(fds, O_NONBLOCK) != 0) {
                                goto bail;
                        }
                        close(fds[1]);
                        fds[1] = -1;
                        dup2(fds[0], fd);
                        close(fds[0]);
                        fds[0] = -1;
                        break;
                case FAULT_READ_STALL:
                        if (g_udp) {
                                /* the socket is sent to somebody else, so it sends fine and never receives */
                                sink = udp_bound(&address);
                                if (sink < 0 ||
                                    connect(fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
                                        goto bail;
                                }
                        } else {
                                /*
                                 * the original read end is kept open, so the wakeup is written
                                 * successfully, and the write end of the replacement stays open,
                                 * so the replacement is empty and not at its end
                                 */
                                keep = dup(fd);
                                if (keep < 0 || pipe2(fds, O_NONBLOCK) != 0) {
                                        goto bail;
                                }
                                dup2(fds[0], fd);
                                close(fds[0]);
                                fds[0] = -1;
                        }
                        break;
                case FAULT_READ_RESET:
                        fds[0] = tcp_reset();
                        if (fds[0] < 0) {
                                goto bail;
                        }
                        dup2(fds[0], fd);
                        close(fds[0]);
                        fds[0] = -1;
                        break;
                case FAULT_UNREPLACEABLE:
                        if (g_udp) {
                                shutdown(fd, SHUT_RDWR);
                        } else {
                                if (pipe2(fds, O_NONBLOCK) != 0) {
                                        goto bail;
                                }
                                close(fds[1]);
                                fds[1] = -1;
                                dup2(fds[0], fd);
                                close(fds[0]);
                                fds[0] = -1;
                        }
                        /* nothing can be created from now on */
                        if (hogs_take() != 0) {
                                fprintf(stderr, "can not take the descriptors\n");
                                goto bail;
                        }
                        break;
        }

        /* what is injected may have replaced the descriptor, the channel to replace is the faulty one */
        id = wakeup_id(find_wakeup_fd(monitor));

        if (fault == FAULT_NONE) {
                /* a healthy channel is not replaced, no matter how often it is used */
                for (i = 0; i < 1000; i++) {
                        medusa_monitor_continue(monitor);
                        if (medusa_monitor_run_timeout(monitor, 0) != 1) {
                                fprintf(stderr, "medusa_monitor_run_timeout failed on a healthy channel\n");
                                goto bail;
                        }
                }
                if (wakeup_id(find_wakeup_fd(monitor)) != id) {
                        fprintf(stderr, "a healthy wakeup channel is replaced\n");
                        goto bail;
                }
                rc = check_wakeup(monitor);
        } else if (fault == FAULT_UNREPLACEABLE) {
                double cpu0;
                unsigned long loops;
                struct timespec start;
                /*
                 * a channel that is closed reports hup for ever. the loop has to be
                 * paced by the timeout of a broken channel, not spin on that.
                 */
                loops = 0;
                cpu0  = cpu();
                clock_gettime(CLOCK_MONOTONIC, &start);
                while (elapsed(&start) < 1.0) {
                        rc = medusa_monitor_run_timeout(monitor, 10.0);
                        if (rc != 1) {
                                fprintf(stderr, "medusa_monitor_run_timeout failed on a channel that can not be replaced, rc: %d\n", rc);
                                goto bail;
                        }
                        loops++;
                }
                if (loops > 5000 || cpu() - cpu0 > 0.5) {
                        fprintf(stderr, "medusa_monitor_run_timeout spins, loops: %lu, cpu: %.2f\n", loops, cpu() - cpu0);
                        goto bail;
                }
                hogs_release();
                /* it recovers by itself as soon as a channel can be created */
                rc = medusa_monitor_run_timeout(monitor, 0);
                if (rc != 1) {
                        fprintf(stderr, "medusa_monitor_run_timeout failed after the descriptors are back, rc: %d\n", rc);
                        goto bail;
                }
                if (wakeup_id(find_wakeup_fd(monitor)) == id) {
                        fprintf(stderr, "wakeup channel is not recreated\n");
                        goto bail;
                }
                rc = check_wakeup(monitor);
        } else if (fault == FAULT_READ_STALL) {
                struct timespec start;
                rc = medusa_monitor_continue(monitor);
                if (rc != 0) {
                        fprintf(stderr, "medusa_monitor_continue failed, rc: %d\n", rc);
                        goto bail;
                }
                /* the loop must neither sleep nor spin for ever on a wakeup that is not read */
                clock_gettime(CLOCK_MONOTONIC, &start);
                for (i = 0; i < 1000; i++) {
                        rc = medusa_monitor_run_timeout(monitor, 10.0);
                        if (rc != 1) {
                                fprintf(stderr, "medusa_monitor_run_timeout failed on a stalled channel, rc: %d\n", rc);
                                goto bail;
                        }
                        if (wakeup_id(find_wakeup_fd(monitor)) != id) {
                                break;
                        }
                }
                if (i == 1000) {
                        fprintf(stderr, "stalled wakeup channel is not recreated\n");
                        goto bail;
                }
                if (elapsed(&start) > 2.0) {
                        fprintf(stderr, "stalled wakeup channel is recreated late, elapsed: %.3f\n", elapsed(&start));
                        goto bail;
                }
                rc = check_wakeup(monitor);
        } else if (fault == FAULT_READ_EOF || fault == FAULT_READ_RESET) {
                /* the loop notices the closed channel and keeps running */
                rc = medusa_monitor_run_timeout(monitor, 1.0);
                if (rc != 1) {
                        fprintf(stderr, "medusa_monitor_run_timeout failed on a closed channel, rc: %d\n", rc);
                        goto bail;
                }
                rc = medusa_monitor_run_timeout(monitor, 0);
                if (rc != 1) {
                        fprintf(stderr, "medusa_monitor_run_timeout failed after a closed channel, rc: %d\n", rc);
                        goto bail;
                }
                if (wakeup_id(find_wakeup_fd(monitor)) == id) {
                        fprintf(stderr, "wakeup channel is not recreated\n");
                        goto bail;
                }
                rc = check_wakeup(monitor);
        } else {
                rc = check_break(monitor, id, recreate);
        }
        if (rc != 0) {
                goto bail;
        }

        if (keep >= 0) {
                close(keep);
        }
        if (extra >= 0) {
                close(extra);
        }
        if (sink >= 0) {
                close(sink);
        }
        if (fds[0] >= 0) {
                close(fds[0]);
        }
        if (fds[1] >= 0) {
                close(fds[1]);
        }
        hogs_release();
        medusa_monitor_destroy(monitor);
        return 0;
bail:   hogs_release();
        if (keep >= 0) {
                close(keep);
        }
        if (extra >= 0) {
                close(extra);
        }
        if (sink >= 0) {
                close(sink);
        }
        if (fds[0] >= 0) {
                close(fds[0]);
        }
        if (fds[1] >= 0) {
                close(fds[1]);
        }
        if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return -1;
}

static int test_poll (unsigned int poll)
{
        int rc;
        int fault;
        int before;
        int after;
        int failed;

        failed = 0;
        before = count_fds();
        for (fault = FAULT_NONE; fault < FAULT_MAX; fault++) {
                rc = test_fault(poll, fault);
                if (rc != 0) {
                        fprintf(stderr, "fault: %s failed\n", fault_string(fault));
                        failed++;
                }
        }
        after = count_fds();
        if (before != after) {
                fprintf(stderr, "fds are leaked, before: %d, after: %d\n", before, after);
                failed++;
        }
        return (failed) ? -1 : 0;
}

static void alarm_handler (int sig)
{
        (void) sig;
        abort();
}

int main (int argc, char *argv[])
{
        int rc;
        int failed;
        unsigned int i;

        (void) argc;
        (void) argv;

        srand(time(NULL));
        signal(SIGALRM, alarm_handler);
        signal(SIGPIPE, SIG_IGN);

        failed = 0;
        for (i = 0; i < sizeof(g_polls) / sizeof(g_polls[0]); i++) {
                alarm(60);
                fprintf(stderr, "testing poll: %d\n", g_polls[i]);
                rc = test_poll(g_polls[i]);
                if (rc != 0) {
                        fprintf(stderr, "poll: %d test failed\n", g_polls[i]);
                        failed++;
                }
        }
        if (failed) {
                return -1;
        }

        fprintf(stderr, "wakeup channel: %s\n", (g_udp) ? "udp socket" : "pipe");
        return 0;
}

#else

int main (int argc, char *argv[])
{
        (void) argc;
        (void) argv;
        fprintf(stderr, "skipping, needs descriptors that are visible through /proc\n");
        return 0;
}

#endif
