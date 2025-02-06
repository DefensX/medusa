
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>

#if defined(__WINDOWS__)
#include <winsock2.h>
#include <wspiapi.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#endif

#include <errno.h>

#include <medusa/error.h>
#include <medusa/clock.h>
#include <medusa/queue.h>
#include <medusa/strndup.h>
#include <medusa/io.h>
#include <medusa/monitor.h>

#include "http_parser.h"

#if !defined(TAILQ_FOREACH_SAFE)
#define TAILQ_FOREACH_SAFE(var, head, field, next)               \
        for ((var) = ((head)->tqh_first);                        \
                (var) && ((next) = ((var)->field.tqe_next), 1);  \
                (var) = (next))
#endif

#define OPTIONS_DEFAULT_REQUESTS        1
#define OPTIONS_DEFAULT_CONCURRENCY     1
#define OPTIONS_DEFAULT_TIMELIMIT       300000
#define OPTIONS_DEFAULT_TIMEOUT         30000
#define OPTIONS_DEFAULT_KEEPALIVE       0
#define OPTIONS_DEFAULT_INTERVAL        0
#define OPTIONS_DEFAULT_VERBOSE         0

#define OPTION_HELP                     'h'
#define OPTION_REQUESTS                 'n'
#define OPTION_CONCURRENCY              'c'
#define OPTION_TIMELIMIT                't'
#define OPTION_TIMEOUT                  's'
#define OPTION_KEEPALIVE                'k'
#define OPTION_INTERVAL                 'i'
#define OPTION_VERBOSE                  'v'
static struct option longopts[] = {
        { "help",               no_argument,            NULL,        OPTION_HELP        },
        { "requests",           required_argument,      NULL,        OPTION_REQUESTS    },
        { "concurrency",        required_argument,      NULL,        OPTION_CONCURRENCY },
        { "timelimit",          required_argument,      NULL,        OPTION_TIMELIMIT   },
        { "timeout",            required_argument,      NULL,        OPTION_TIMEOUT     },
        { "keepalive",          required_argument,      NULL,        OPTION_KEEPALIVE   },
        { "interval",           required_argument,      NULL,        OPTION_INTERVAL    },
        { "verbose",            required_argument,      NULL,        OPTION_VERBOSE     },
        { NULL,                 0,                      NULL,        0                  },
};

static void usage (const char *pname)
{
        fprintf(stdout, "medusa http http benchmarking tool\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "usage:\n");
        fprintf(stdout, "  %s [options] [http[s]://]hostname[:port]/path\n", pname);
        fprintf(stdout, "\n");
        fprintf(stdout, "options:\n");
        fprintf(stdout, "  -n, --requests   : number of requests to perform (default: %d)\n", OPTIONS_DEFAULT_REQUESTS);
        fprintf(stdout, "  -c, --concurrency: number of multiple requests to make at a time (default: %d)\n", OPTIONS_DEFAULT_CONCURRENCY);
        fprintf(stdout, "  -t, --timelimit  : milliseconds to spend on benchmarking (default: %d)\n", OPTIONS_DEFAULT_TIMELIMIT);
        fprintf(stdout, "  -s, --timeout    : milliseconds to max. wait for each response (default: %d)\n", OPTIONS_DEFAULT_TIMEOUT);
        fprintf(stdout, "  -k, --keepalive  : use http keepalive feature (default: %d)\n", OPTIONS_DEFAULT_KEEPALIVE);
        fprintf(stdout, "  -i, --interval   : milliseconds interval between requests (default: %d)\n", OPTIONS_DEFAULT_INTERVAL);
        fprintf(stdout, "  -v, --verbose    : set verbose level (default: %d)\n", OPTIONS_DEFAULT_VERBOSE);
        fprintf(stdout, "  -h, --help       : this text\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "tuning:\n");
        fprintf(stdout, "  change local port range:\n");
        fprintf(stdout, "    echo \"MIN MAX\" > /proc/sys/net/ipv4/ip_local_port_range\n");
        fprintf(stdout, "  allow tcp timewait ports to be used\n");
        fprintf(stdout, "    echo \"1\" > /proc/sys/net/ipv4/tcp_tw_reuse\n");
}

struct options {
        long long requests;
        long long concurrency;
        long long timelimit;
        long long timeout;
        long long interval;
        int keepalive;
        const char *url;
};

struct url {
        char *scheme;
        char *host;
        unsigned short port;
        char *path;
};

struct buffer {
        char *buffer;
        long long offset;
        long long length;
        long long size;
        long long refcount;
};

TAILQ_HEAD(ports, port);
struct port {
        TAILQ_ENTRY(port) ports;
        int port;
        int error;
};

enum {
        CLIENT_STATE_UNKNOWN,
        CLIENT_STATE_CONNECTING,
        CLIENT_STATE_CONNECTED,
        CLIENT_STATE_REQUESTING,
        CLIENT_STATE_REQUESTED,
        CLIENT_STATE_PARSING,
        CLIENT_STATE_PARSED,
        CLIENT_STATE_DISDONNECTING,
        CLIENT_STATE_DISCONNECTED,
        CLIENT_STATE_FINISHED,
};

TAILQ_HEAD(clients, client);
struct client {
        TAILQ_ENTRY(client) clients;
        struct medusa_io *io;
        unsigned int state;
        struct port *port;
        struct ports *ports;
        struct buffer *request;
        long long request_offset;
        unsigned long connect_timestamp;
        struct buffer incoming;
        http_parser http_parse;
};

enum {
        DEBUG_LEVEL_ASSERT,
        DEBUG_LEVEL_ERROR,
        DEBUG_LEVEL_INFO,
        DEBUG_LEVEL_DEBUG
};
static int g_debug_level = DEBUG_LEVEL_ERROR;

#define debugf(fmt...) { \
        if (g_debug_level >= DEBUG_LEVEL_DEBUG) { \
                fprintf(stderr, "debug: "); \
                fprintf(stderr, fmt); \
                fprintf(stderr, " (%s %s:%d)\n", __FUNCTION__, __FILE__, __LINE__); \
        } \
}

#define infof(fmt...) { \
        if (g_debug_level >= DEBUG_LEVEL_INFO) { \
                fprintf(stderr, "info: "); \
                fprintf(stderr, fmt); \
                fprintf(stderr, " (%s %s:%d)\n", __FUNCTION__, __FILE__, __LINE__); \
        } \
}

#define errorf(fmt...) { \
        if (g_debug_level >= DEBUG_LEVEL_ERROR) { \
                fprintf(stderr, "error: "); \
                fprintf(stderr, fmt); \
                fprintf(stderr, " (%s %s:%d)\n", __FUNCTION__, __FILE__, __LINE__); \
        } \
}

static int client_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param);

static unsigned long clock_get (void)
{
        int rc;
        struct timespec ts;
        unsigned long long tsec;
        unsigned long long tusec;
        unsigned long long _clock;
        rc = medusa_clock_monotonic_raw(&ts);
        if (rc < 0) {
                return 0;
        }
        tsec = ((unsigned long long) ts.tv_sec) * 1000;
        tusec = ((unsigned long long) ts.tv_nsec) / 1000 / 1000;
        _clock = tsec + tusec;
        return _clock;
}

static int clock_after (unsigned long a, unsigned long b)
{
        return (((long) ((b) - (a)) < 0)) ? 1 : 0;
}
#define clock_before(a,b)        clock_after(b,a)

static int buffer_resize (struct buffer *buffer, long long size)
{
        void *data;
        if (buffer == NULL) {
                return -1;
        }
        if (buffer->size >= size) {
                return 0;
        }
        data = realloc(buffer->buffer, size);
        if (data == NULL) {
                data = malloc(size);
                if (data == NULL) {
                        errorf("can not allocate memory");
                        return -1;
                }
                if (buffer->length > 0) {
                        memcpy(data, buffer->buffer, buffer->length);
                }
                free(buffer->buffer);
        }
        buffer->buffer = (char *) data;
        buffer->size = size;
        return 0;
}

static int buffer_grow (struct buffer *buffer, long long size)
{
        return buffer_resize(buffer, buffer->length + size);
}

static int buffer_printf (struct buffer *buffer, const char *format, ...)
{
        int rc;
        long long size;
        va_list va;
        if (buffer == NULL) {
                errorf("buffer is invalid");
                goto bail;
        }
        if (format == NULL) {
                errorf("format is invalid");
                goto bail;
        }
        va_start(va, format);
        size = vsnprintf(NULL, 0, format, va);
        va_end(va);
        if (size < 0) {
                errorf("can not allocate memory");
                goto bail;
        }
        rc = buffer_grow(buffer, size + 1);
        if (rc != 0) {
                errorf("can not grow buffer");
                goto bail;
        }
        va_start(va, format);
        rc = vsnprintf(buffer->buffer + buffer->length, size + 1, format, va);
        va_end(va);
        if (rc <= 0) {
                errorf("can not allocate memory");
                goto bail;
        }
        buffer->length += rc;
        return 0;
bail:   return -1;
}

static int buffer_shift (struct buffer *buffer, unsigned int length)
{
        if (length == 0) {
                return 0;
        }
        if (length > buffer->length) {
                errorf("invalid length");
                return -1;
        }
        memmove(buffer->buffer, buffer->buffer + length, buffer->length - length);
        buffer->length -= length;
        return 0;
}

static int buffer_reset (struct buffer *buffer)
{
        if (buffer == NULL) {
                return -1;
        }
        buffer->length = 0;
        buffer->offset = 0;
        return 0;
}

static void buffer_uninit (struct buffer *buffer)
{
        if (buffer == NULL) {
                return;
        }
        if (--buffer->refcount > 0) {
                return;
        }
        if (buffer->buffer != NULL) {
                free(buffer->buffer);
        }
        memset(buffer, 0, sizeof(struct buffer));
}

static int buffer_init (struct buffer *buffer)
{
        if (buffer == NULL) {
                goto bail;
        }
        memset(buffer, 0, sizeof(struct buffer));
        buffer->refcount = 1;
        return 0;
bail:   if (buffer != NULL) {
                buffer_uninit(buffer);
        }
        return -1;
}

static void buffer_destroy (struct buffer *buffer)
{
        if (buffer == NULL) {
                return;
        }
        if (--buffer->refcount > 0) {
                return;
        }
        if (buffer->buffer != NULL) {
                free(buffer->buffer);
        }
        free(buffer);
}

static struct buffer * buffer_create (void)
{
        struct buffer *buffer;
        buffer = (struct buffer *) malloc(sizeof(struct buffer));
        if (buffer == NULL) {
                errorf("can not allocate memory");
                goto bail;
        }
        memset(buffer, 0, sizeof(struct buffer));
        buffer->refcount = 1;
        return buffer;
bail:   if (buffer != NULL) {
                buffer_destroy(buffer);
        }
        return NULL;
}

static void url_destroy (struct url *url)
{
        if (url == NULL) {
                return;
        }
        if (url->scheme != NULL) {
                free(url->scheme);
        }
        if (url->host != NULL) {
                free(url->host);
        }
        if (url->path != NULL) {
                free(url->path);
        }
        free(url);
}

static struct url * url_create (const char *uri)
{
        char *i;
        char *p;
        char *e;
        char *t;
        char *u;
        struct url *url;
        u = NULL;
        p = NULL;
        e = NULL;
        url = NULL;
        if (uri == NULL) {
                errorf("uri is invalid");
                goto bail;
        }
        url = (struct url *) malloc(sizeof(struct url));
        if (url == NULL) {
                errorf("can not allocate memory");
                goto bail;
        }
        memset(url, 0, sizeof(struct url));
        u = strdup(uri);
        if (u == NULL) {
                errorf("can not allocate memory");
                goto bail;
        }
        if (u[0] == '<') {
                memmove(u, u + 1, strlen(u) - 1);
                t = strchr(u, '>');
                if (t != NULL) {
                        *t = '\0';
                }
        }
        i = strstr(u, "://");
        if (i != NULL) {
                url->scheme = medusa_strndup(u, i - u);
                i += 3;
        } else {
                i = u;
        }
        if (i != NULL) {
                p = strchr(i, ':');
                e = strchr(i, '/');
        }
        if (p == NULL && e == NULL) {
                url->port = 0;
                url->host = strdup(i);
        } else if (p != NULL && e == NULL) {
                url->port = atoi(p + 1);
                url->host = medusa_strndup(i, p - i);
        } else if (p == NULL || e < p) {
                url->port = 0;
                if (e == NULL) {
                        url->host = strdup(i);
                } else {
                        *e = '\0';
                        url->host = medusa_strndup(i, e - i);
                }
        } else {
                if (e != NULL) {
                        *e = '\0';
                }
                url->port = atoi(p + 1);
                url->host = medusa_strndup(i, p - i);
        }
        if (e != NULL) {
                do {
                        e++;
                } while (*e == '/');
                url->path = strdup(e);
        }
        if (url->port == 0 &&
            url->scheme != NULL) {
                if (strcmp(url->scheme, "http") == 0) {
                        url->port = 80;
                }
        }
        free(u);
        return url;
bail:   if (url != NULL) {
                url_destroy(url);
        }
        if (u != NULL) {
                free(u);
        }
        return NULL;
}

static  char * read_proc_file (const char *file)
{
        int fd;
        ssize_t rc;
        char *buffer;
        #define PROC_FILE_BUFFER_SIZE (64 * 1024)
        fd = open(file, O_RDONLY);
        if (fd < 0) {
                printf("open failed for: %s\n", file);
                return NULL;
        }
        buffer = (char *) malloc(PROC_FILE_BUFFER_SIZE);
        if (buffer == NULL) {
                printf("malloc failed\n");
                close(fd);
                return NULL;
        }
        rc = read(fd, buffer, PROC_FILE_BUFFER_SIZE - 1);
        if (rc < 0) {
                printf("read failed\n");
                close(fd);
                free(buffer);
                return NULL;
        }
        buffer[rc] = '\0';
        close(fd);
        return buffer;
}

static int get_ip_local_port_range (int *min, int *max)
{
        int rc;
        char *buffer;
        buffer = NULL;
        if (min == NULL) {
                errorf("min is invalid");
                goto bail;
        }
        if (max == NULL) {
                errorf("max is invalid");
                goto bail;
        }
        *min = 0;
        *max = 0;
        buffer = read_proc_file("/proc/sys/net/ipv4/ip_local_port_range");
        if (buffer == NULL) {
                errorf("can not read file");
                goto bail;
        }
        rc = sscanf(buffer, "%d %d", min, max);
        if (rc != 2) {
                errorf("can not parse buffer");
                goto bail;
        }
        if (*max <= *min) {
                errorf("range is invalid");
                goto bail;
        }
        free(buffer);
        return 0;
bail:   if (buffer != NULL) {
                free(buffer);
        }
        if (min != NULL) {
                *min = 0;
        }
        if (max != NULL) {
                *max = 0;
        }
        return -1;
}

static void port_destroy (struct port *port)
{
        if (port == NULL) {
                return;
        }
        free(port);
}

static struct port * port_create (int number)
{
        struct port *port;
        port = (struct port *) malloc(sizeof(struct port));
        if (port == NULL) {
                errorf("can not allocate memory");
                goto bail;
        }
        memset(port, 0, sizeof(struct port));
        port->port = number;
        return port;
bail:   if (port != NULL) {
                port_destroy(port);
        }
        return NULL;
}

static struct port * ports_pop (struct ports *ports)
{
        struct port *port;
        struct port *nport;
        if (ports == NULL) {
                return NULL;
        }
        if (TAILQ_EMPTY(ports)) {
                return NULL;
        }
        TAILQ_FOREACH_SAFE(port, ports, ports, nport) {
                if (port->error >= 1) {
                        continue;
                }
                TAILQ_REMOVE(ports, port, ports);
                return port;
        }
        return NULL;
}

static int ports_push (struct ports *ports, struct port *port)
{
        if (ports == NULL) {
                return -1;
        }
        if (port == NULL) {
                return -1;
        }
        TAILQ_INSERT_TAIL(ports, port, ports);
        return 0;
}

static int client_get_fd_error (struct client *client)
{
        int rc;
        int error;
        socklen_t len;
        len = sizeof(error);
        if (client == NULL) {
                return -EINVAL;
        }
        if (client->io == NULL) {
                return -EINVAL;
        }
        rc = getsockopt(medusa_io_get_fd(client->io), SOL_SOCKET, SO_ERROR, (void *) &error, &len);
        if (rc != 0) {
                return -EINVAL;
        }
        if (error == 0) {
                return 0;
        }
        return -error;
}

static int client_disconnect (struct client *client)
{
        if (client == NULL) {
                return -1;
        }
        if (client->io != NULL) {
                medusa_io_destroy(client->io);
                client->io = NULL;
        }
        if (client->port != NULL) {
                ports_push(client->ports, client->port);
                client->port = NULL;
        }
        return 0;
}

static int client_connect (struct medusa_monitor *monitor, struct client *client, const char *address, int port)
{
        int rc;
        int fd;
        struct sockaddr_in sin;
        fd = -1;
        if (client == NULL) {
                errorf("client is invalid");
                rc = -EIO;
                goto bail;
        }
        if (address == NULL) {
                errorf("address is invalid");
                rc = -EIO;
                goto bail;
        }
        if (port <= 0 ||
            port >= 0xffff) {
                errorf("port is invalid");
                rc = -EIO;
                goto bail;
        }
again:
        client_disconnect(client);
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
                errorf("can not open socket");
                rc = -EIO;
                goto bail;
        }
#if defined(__WINDOWS__)
        unsigned long nonblocking = 1;
        rc = ioctlsocket(fd, FIONBIO, &nonblocking);
#else
        int flags;
        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
                errorf("can not get flags");
                rc = -EIO;
                goto bail;
        }
        flags = flags | O_NONBLOCK;
        rc = fcntl(fd, F_SETFL, flags);
#endif
        if (rc != 0) {
                errorf("can not set flags");
                rc = -EIO;
                goto bail;
        }
        client->io = medusa_io_create(monitor, fd, client_io_onevent, client);
        if (MEDUSA_IS_ERR_OR_NULL(client->io)) {
                errorf("can not create io");
                rc = -EIO;
                goto bail;
        }
        if (1) {
                struct port *lport;
                struct sockaddr_in laddr;
                memset(&laddr, 0, sizeof(laddr));
                laddr.sin_family = AF_INET;
                laddr.sin_addr.s_addr = htonl(INADDR_ANY);
                while (1) {
                        lport = ports_pop(client->ports);
                        if (lport == NULL) {
                                errorf("can not get local port");
                                rc = -EIO;
                                goto bail;
                        }
                        laddr.sin_port = htons(lport->port);
#if defined(SO_REUSEPORT)
                        int opt = 1;
                        rc = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
                        if (rc < 0) {
                                errorf("setsockopt reuseport failed");
                                goto bail;
                        }
#endif
                        rc = bind(fd, (struct sockaddr*) &laddr, sizeof(laddr));
                        if (rc != 0) {
                                errorf("can not bind to port: %d, error: %d, %s", lport->port, errno, strerror(errno));
                                lport->error += 1;
                                ports_push(client->ports, lport);
                                continue;
                        }
                        lport->error = 0;
                        break;
                }
                client->port = lport;
        }
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        inet_pton(AF_INET, address, &sin.sin_addr);
        sin.sin_port = htons(port);
        rc = connect(fd, (struct sockaddr *) &sin, sizeof(sin));
        if (rc != 0) {
                if (errno == EINPROGRESS) {
                        rc = 0;
                } else if (errno == EADDRNOTAVAIL) {
                        debugf("address not available (%d -> %d)", client->port->port, port);
                        goto again;
                } else {
                        rc = -errno;
                        goto bail;
                }
        }
        return rc;
bail:   if (client != NULL) {
                if (fd >= 0) {
                        client_disconnect(client);
                }
        }
        return rc;
}

static void client_destroy (struct client *client)
{
        if (client == NULL) {
                return;
        }
        client_disconnect(client);
        buffer_uninit(&client->incoming);
        free(client);
}

static struct client * client_create (struct ports *ports, struct buffer *request)
{
        struct client *client;
        client = (struct client *) malloc(sizeof(struct client));
        if (client == NULL) {
                errorf("can not allocate memory");
                goto bail;
        }
        memset(client, 0, sizeof(struct client));
        client->io = NULL;
        client->request = request;
        client->request_offset = 0;
        client->ports = ports;
        client->state = CLIENT_STATE_DISCONNECTED;
        client->http_parse.data = client;
        http_parser_init(&client->http_parse, HTTP_RESPONSE);
        buffer_init(&client->incoming);
        return client;
bail:   if (client != NULL) {
                client_destroy(client);
        }
        return NULL;
}

static int http_parser_on_message_begin (http_parser *http_parser)
{
        struct client *client = (struct client *) http_parser->data;
        (void) client;
        debugf("client: %p, message-begin", client);
        return 0;
}

static int http_parser_on_message_complete (http_parser *http_parser)
{
        struct client *client = (struct client *) http_parser->data;
        debugf("client: %p, message-complete", client);
        client->state = CLIENT_STATE_PARSED;
        return 0;
}

static int http_parser_on_header_field (http_parser *http_parser, const char *at, size_t length)
{
        struct client *client = (struct client *) http_parser->data;
        (void) client;
        (void) at;
        (void) length;
        debugf("client: %p, header-field: %.*s", client, (int) length, at);
        return 0;
}

static int http_parser_on_header_value (http_parser *http_parser, const char *at, size_t length)
{
        struct client *client = (struct client *) http_parser->data;
        (void) client;
        (void) at;
        (void) length;
        debugf("client: %p, header-value: %.*s", client, (int) length, at);
        return 0;
}

static int client_io_onevent (struct medusa_io *io, unsigned int events, void *context, void *param)
{
        int rc;
        struct client *client = (struct client *) context;
        (void) param;
        if ((events & MEDUSA_IO_EVENT_ERR) ||
            (events & MEDUSA_IO_EVENT_HUP) ||
            (events & MEDUSA_IO_EVENT_NVAL)) {
                client->state = CLIENT_STATE_DISDONNECTING;
        } else if (events & MEDUSA_IO_EVENT_IN) {
                ssize_t read_rc;
                rc = buffer_resize(&client->incoming, client->incoming.length + 1024);
                if (rc != 0) {
                        errorf("can not reserve client buffer");
                        client->state = CLIENT_STATE_DISDONNECTING;
                        goto bail;
                }
                read_rc = read(medusa_io_get_fd(io),
                               client->incoming.buffer + client->incoming.length,
                               client->incoming.size - client->incoming.length);
                if (read_rc == 0) {
                        errorf("connection reset by server (state: %d)", client->state);
                        client->state = CLIENT_STATE_DISDONNECTING;
                        goto out;
                } else if (read_rc < 0) {
                        if (errno == EINTR) {
                        } else if (errno == EAGAIN) {
                        } else if (errno == EWOULDBLOCK) {
                        } else {
                                errorf("connection reset by server, rc: %d, error: %d, %s", rc, errno, strerror(errno));
                                client->state = CLIENT_STATE_DISDONNECTING;
                                goto out;
                        }
                } else {
                        client->incoming.length += read_rc;
                }
        } else if (events & MEDUSA_IO_EVENT_OUT) {
                if (client->state == CLIENT_STATE_CONNECTING) {
                        rc = client_get_fd_error(client);
                        if (rc == 0) {
                                client->state = CLIENT_STATE_CONNECTED;
                        } else {
                                errorf("client: %p failed to connect: %d", client, rc);
                                client->state = CLIENT_STATE_DISDONNECTING;
                                goto bail;
                        }
                } else if (client->state == CLIENT_STATE_REQUESTING) {
                        ssize_t write_rc;
                        write_rc = write(medusa_io_get_fd(io),
                                        client->request->buffer + client->request_offset,
                                        client->request->length - client->request_offset);
                        if (write_rc == 0) {
                                errorf("can not write to client: %p, request: %lld, error: %d, %s", client, client->request->length - client->request_offset, errno, strerror(errno));
                                client->state = CLIENT_STATE_DISDONNECTING;
                                goto bail;
                        } else if (write_rc < 0) {
                                if (errno == EINTR) {
                                } else if (errno == EAGAIN) {
                                } else if (errno == EWOULDBLOCK) {
                                } else {
                                        errorf("can not write client: %p error: %d, %s", client, errno, strerror(errno));
                                        client->state = CLIENT_STATE_DISDONNECTING;
                                        goto bail;
                                }
                        } else {
                                client->request_offset += write_rc;
                                if (client->request_offset == client->request->length) {
                                        client->state = CLIENT_STATE_REQUESTED;
                                }
                        }
                } else {
                        errorf("invalid client: %p, state: %d", client, client->state);
                        client->state = CLIENT_STATE_REQUESTED;
                        goto bail;
                }
        } else if (events & MEDUSA_IO_EVENT_DESTROY) {
                int fd;
                fd = medusa_io_get_fd(io);
                if (fd >= 0) {
                        close(fd);
                }
        } else if (events != 0) {
                errorf("invalid client: %p, state: %d", client, client->state);
                client->state = CLIENT_STATE_REQUESTED;
                goto bail;
        }
out:    return 0;
bail:   return -1;
}

int main (int argc, char *argv[])
{
        int c;
        int rc;
        int ret;

        struct options options;
        long long nclients;
        struct clients clients;

        long long i;

        struct url *url;
        struct buffer *request;

        const char *url_scheme;
        char url_address[INET_ADDRSTRLEN];
        int url_port;
        const char *url_path;

        int p;
        struct port *port;
        struct port *nport;
        struct ports ports;
        int port_range_min;
        int port_range_max;

        struct client *client;
        struct client *nclient;

        struct medusa_monitor *monitor;

        ret = 0;

        memset(&options, 0, sizeof(struct options));
        options.requests = OPTIONS_DEFAULT_REQUESTS;
        options.concurrency = OPTIONS_DEFAULT_CONCURRENCY;
        options.timelimit = OPTIONS_DEFAULT_TIMELIMIT;
        options.timeout = OPTIONS_DEFAULT_TIMEOUT;
        options.keepalive = OPTIONS_DEFAULT_KEEPALIVE;
        options.url = NULL;

        url = NULL;
        request = NULL;

        monitor = NULL;

        TAILQ_INIT(&ports);
        port_range_min = 0;
        port_range_max = 0;

        nclients = 0;
        TAILQ_INIT(&clients);

        while ((c = getopt_long(argc, argv, "hn:c:t:s:k:i:v:", longopts, NULL)) != -1) {
                switch (c) {
                        case OPTION_HELP:
                                usage(argv[0]);
                                goto out;
                        case OPTION_REQUESTS:
                                options.requests = atoll(optarg);
                                break;
                        case OPTION_CONCURRENCY:
                                options.concurrency = atoll(optarg);
                                break;
                        case OPTION_TIMELIMIT:
                                options.timelimit = atoll(optarg);
                                break;
                        case OPTION_TIMEOUT:
                                options.timeout = atoll(optarg);
                                break;
                        case OPTION_KEEPALIVE:
                                options.keepalive = !!atoi(optarg);
                                break;
                        case OPTION_INTERVAL:
                                options.interval = atoll(optarg);
                                break;
                        case OPTION_VERBOSE:
                                g_debug_level = atoi(optarg);
                                break;
                        default:
                                fprintf(stderr, "invalid option: %s\n", argv[optind - 1]);
                                goto bail;
                }
        }

        if (optind + 1 != argc) {
                fprintf(stderr, "url parameter is missing\n");
                goto bail;
        }
        options.url = argv[optind++];

        rc = get_ip_local_port_range(&port_range_min, &port_range_max);
        if (rc != 0) {
                errorf("can not get local port range");
                goto bail;
        }
        if (options.concurrency > port_range_max - port_range_min) {
                errorf("local port range (%d - %d = %d) is not enough for requested concurrency (%lld)", port_range_min, port_range_max, port_range_max - port_range_min, options.concurrency);
                errorf("set local port range with");
                errorf("echo \"MIN MAX\" > /proc/sys/net/ipv4/ip_local_port_range");
                goto bail;
        }
        for (p = port_range_min; p < port_range_max; p++) {
                port = port_create(p);
                if (port == NULL) {
                        errorf("can not create port");
                        goto bail;
                }
                TAILQ_INSERT_TAIL(&ports, port, ports);
        }

        fprintf(stdout, "medusa http benchmark\n");
        fprintf(stdout, "\n");
        fprintf(stdout, "options:\n");
        fprintf(stdout, "  requests   : %lld\n", options.requests);
        fprintf(stdout, "  concurrency: %lld\n", options.concurrency);
        fprintf(stdout, "  timelimit  : %lld\n", options.timelimit);
        fprintf(stdout, "  timeout    : %lld\n", options.timeout);
        fprintf(stdout, "  interval   : %lld\n", options.interval);
        fprintf(stdout, "  keepalive  : %d\n", options.keepalive);
        fprintf(stdout, "  url        : %s\n", options.url);
        fprintf(stdout, "\n");
        fprintf(stdout, "memory:\n");
        fprintf(stdout, "  url   : %zd bytes\n", sizeof(struct url));
        fprintf(stdout, "  buffer: %zd bytes\n", sizeof(struct buffer));
        fprintf(stdout, "  client: %zd bytes\n", sizeof(struct client));
        fprintf(stdout, "port range:\n");
        fprintf(stdout, "  minimum: %d\n", port_range_min);
        fprintf(stdout, "  maximum: %d\n", port_range_max);

        url = url_create(options.url);
        if (url == NULL) {
                errorf("url is invalid");
                goto bail;
        }
        url_port = 0;
        url_address[0] = '\0';
        {
                int rc;
                struct addrinfo hints;
                struct addrinfo *result;
                struct addrinfo *res;
                result = NULL;
                memset(&hints, 0, sizeof(struct addrinfo));
                hints.ai_family = PF_UNSPEC;
                hints.ai_socktype = SOCK_STREAM;
                rc = getaddrinfo(url->host, NULL, &hints, &result);
                if (rc != 0) {
                        errorf("getaddrinfo failed for: %s", url->host);
                        goto bail;
                }
                for (res = result; res; res = res->ai_next) {
                        if (res->ai_family != AF_INET) {
                                continue;
                        }
                        if (inet_ntop(AF_INET, &(((struct sockaddr_in *) res->ai_addr)->sin_addr), url_address, sizeof(url_address)) == NULL) {
                                url_port = 0;
                                url_address[0] = '\0';
                                continue;
                        }
                        break;
                }
                freeaddrinfo(result);
        }

        if (strlen(url_address) == 0) {
                errorf("can not resolve host: %s", url->host);
                goto bail;
        }
        url_scheme = (url->scheme == NULL) ? "http" : url->scheme;
        url_port = (url->port == 0) ? 80 : url->port;
        url_path = (url->path == NULL || strlen(url->path) == 0) ? "" : url->path;

        fprintf(stdout, "\n");
        fprintf(stdout, "benchmarking %s://%s:%d/%s ...\n",
                        url_scheme,
                        url_address,
                        url_port,
                        url_path);

        request = buffer_create();
        if (request == NULL) {
                errorf("can not create request");
                goto bail;
        }
        rc = buffer_printf(request,
                        "GET /%s HTTP/1.0\r\n"
                        "%s"
                        "Host: %s\r\n"
                        "User-Agent: %s\r\n"
                        "Accept: */*\r\n"
                        "\r\n",
                        url_path,
                        (options.keepalive) ? "Connection: Keep-Alive\r\n" : "",
                        url_address,
                        "MedusaHTTPBenchmark/1.0");
        if (rc != 0) {
                errorf("can not build request");
                goto bail;
        }

        if (g_debug_level >= DEBUG_LEVEL_INFO) {
                fprintf(stdout, "---\n");
                fprintf(stdout, "%s", (char *) request->buffer);
                fprintf(stdout, "---\n");
        }

        for (i = 0; i < options.concurrency; i++) {
                client = client_create(&ports, request);
                if (client == NULL) {
                        errorf("can not create client");
                        goto bail;
                }
                TAILQ_INSERT_TAIL(&clients, client, clients);
                nclients += 1;
        }

        monitor = medusa_monitor_create_with_options(NULL);
        if (monitor == NULL) {
                errorf("can not create monitor");
                goto bail;
        }

        while (1) {
                TAILQ_FOREACH_SAFE(client, &clients, clients, nclient) {
                        if (client->state == CLIENT_STATE_CONNECTING ||
                            client->state == CLIENT_STATE_REQUESTING ||
                            client->state == CLIENT_STATE_PARSING){
                                unsigned long current;
                                current = clock_get();
                                if (clock_after(current, client->connect_timestamp + options.timeout)) {
                                        errorf("client: %p, state: %d timeout", client, client->state);
                                        client->state = CLIENT_STATE_DISDONNECTING;
                                }
                        }
                        if (client->state == CLIENT_STATE_CONNECTING) {
                                debugf("client: %p, state: connecting", client);
                        }
                        if (client->state == CLIENT_STATE_CONNECTED) {
                                debugf("client: %p, state: connected", client);
                                client->state = CLIENT_STATE_REQUESTING;
                                rc = medusa_io_set_events(client->io, MEDUSA_IO_EVENT_OUT);
                                if (rc < 0) {
                                        errorf("can not add client to monitor");
                                        goto bail;
                                }
                        }
                        if (client->state == CLIENT_STATE_REQUESTING) {
                                debugf("client: %p, state: requesting", client);
                        }
                        if (client->state == CLIENT_STATE_REQUESTED) {
                                debugf("client: %p, state: requested", client);
                                client->state = CLIENT_STATE_PARSING;
                                rc = medusa_io_set_events(client->io, MEDUSA_IO_EVENT_IN);
                                if (rc < 0) {
                                        errorf("can not add client to monitor");
                                        goto bail;
                                }
                        }
                        if (client->state == CLIENT_STATE_PARSING) {
                                size_t nparsed;
                                http_parser_settings settings;
                                debugf("client: %p, state: parsing (size: %lld, length: %lld, offset: %lld)",
                                                client,
                                                client->incoming.size,
                                                client->incoming.length,
                                                client->incoming.offset);
                                if (client->incoming.length - client->incoming.offset > 0) {
                                        http_parser_settings_init(&settings);
                                        settings.on_message_begin    = http_parser_on_message_begin;
                                        settings.on_header_field     = http_parser_on_header_field;
                                        settings.on_header_value     = http_parser_on_header_value;
                                        settings.on_message_complete = http_parser_on_message_complete;
                                        nparsed = http_parser_execute(&client->http_parse, &settings,
                                                        client->incoming.buffer + client->incoming.offset,
                                                        client->incoming.length - client->incoming.offset);
                                        if (nparsed > 0) {
#if 1
                                                buffer_shift(&client->incoming, nparsed);
#else
                                                client->incoming.offset += nparsed;
#endif
                                        }
                                }
                        }
                        if (client->state == CLIENT_STATE_PARSED) {
                                debugf("client: %p, state: parsed", client);
                                client->state = CLIENT_STATE_DISDONNECTING;
                        }
                        if (client->state == CLIENT_STATE_DISDONNECTING) {
                                debugf("client: %p, state: disconnecting, requests: %lld", client, options.requests);
                                if (options.requests <= 0) {
                                        if (client->io != NULL) {
                                                medusa_io_destroy(client->io);
                                        }
                                        if (client->port != NULL) {
                                                ports_push(client->ports, client->port);
                                        }
                                        client->io = NULL;
                                        client->port = NULL;
                                        client->state = CLIENT_STATE_FINISHED;
                                } else {
                                        if (options.keepalive == 0) {
                                                if (client->io != NULL) {
                                                        medusa_io_destroy(client->io);
                                                }
                                                if (client->port != NULL) {
                                                        ports_push(client->ports, client->port);
                                                }
                                                client->io = NULL;
                                                client->port = NULL;
                                        } else {
                                                debugf("client: %p, state: keep-alive", client);
                                        }
                                        client->state = CLIENT_STATE_DISCONNECTED;
                                }
                                client->request_offset = 0;
                                client->http_parse.data = client;
                                http_parser_init(&client->http_parse, HTTP_RESPONSE);
                                buffer_reset(&client->incoming);
                        }
                        if (client->state == CLIENT_STATE_DISCONNECTED) {
                                unsigned long current;
                                current = clock_get();
                                if (client->io == NULL ||
                                    options.keepalive == 0) {
                                        debugf("client disconnected");
                                }
                                if (clock_after(current, client->connect_timestamp + options.interval)) {
                                        client->request_offset = 0;
                                        client->http_parse.data = client;
                                        http_parser_init(&client->http_parse, HTTP_RESPONSE);
                                        buffer_reset(&client->incoming);
                                        if (client->io != NULL &&
                                            options.keepalive != 0) {
                                                options.requests -= 1;
                                                client->state = CLIENT_STATE_REQUESTING;
                                                client->connect_timestamp = clock_get();
                                                rc = medusa_io_set_events(client->io, MEDUSA_IO_EVENT_OUT);
                                                if (rc < 0) {
                                                        errorf("can not add client to monitor");
                                                        goto bail;
                                                }
                                        } else {
                                                if (options.requests > 0) {
                                                        rc = client_connect(monitor, client, url_address, url_port);
                                                        if (rc != 0) {
                                                                errorf("can not connect client: %p to %s:%d, rc: %d, %s", client, url_address, url_port, rc, strerror(-rc));
                                                                client->state = CLIENT_STATE_DISDONNECTING;
                                                                goto bail;
                                                        }
                                                        client->state = CLIENT_STATE_CONNECTING;
                                                        options.requests -= 1;
                                                        client->connect_timestamp = clock_get();
                                                        rc  = medusa_io_set_events(client->io, MEDUSA_IO_EVENT_OUT);
                                                        rc |= medusa_io_set_enabled(client->io, 1);
                                                        if (rc < 0) {
                                                                errorf("can not add client to monitor");
                                                                goto bail;
                                                        }
                                                } else {
                                                        if (client->io != NULL) {
                                                                medusa_io_destroy(client->io);
                                                        }
                                                        if (client->port != NULL) {
                                                                ports_push(client->ports, client->port);
                                                        }
                                                        client->io = NULL;
                                                        client->port = NULL;
                                                        client->state = CLIENT_STATE_FINISHED;
                                                }
                                        }
                                }
                        }
                        if (client->state == CLIENT_STATE_FINISHED) {
                                debugf("client: %p, state: finished", client);
                        }
                }

                TAILQ_FOREACH_SAFE(client, &clients, clients, nclient) {
                        if (client->state != CLIENT_STATE_FINISHED) {
                                break;
                        }
                }
                if (client == NULL) {
                        debugf("no more clients");
                        break;
                }
                rc = medusa_monitor_run_timeout(monitor, 1.0);
                if (rc < 0) {
                        errorf("monitor failed");
                        goto bail;
                }
        }

        fprintf(stdout, "done\n");

out:    TAILQ_FOREACH_SAFE(client, &clients, clients, nclient) {
                TAILQ_REMOVE(&clients, client, clients);
                client_destroy(client);
                nclients = nclients - 1;
        }
        TAILQ_FOREACH_SAFE(port, &ports, ports, nport) {
                TAILQ_REMOVE(&ports, port, ports);
                port_destroy(port);
        }
        if (url != NULL) {
                url_destroy(url);
        }
        if (request != NULL) {
                buffer_destroy(request);
        }
        if (monitor != NULL) {
                medusa_monitor_destroy(monitor);
        }
        return ret;
bail:   ret = -1;
        goto out;
}
