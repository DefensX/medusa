
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "error.h"
#include "url.h"

struct medusa_url {
        char *base;
        char *scheme;
        char *host;
        unsigned short port;
        char *path;
};

static const struct {
        const char *scheme;
        unsigned short port;
} g_services[] = {
	{ "acap",      674 },
	{ "cap",       1026 },
	{ "dict",      2628 },
	{ "ftp",       21 },
	{ "gopher",    70 },
	{ "http",      80 },
	{ "https",     443 },
	{ "iax",       4569 },
	{ "icap",      1344 },
	{ "imap",      143 },
	{ "ipp",       631 },
	{ "ldap",      389 },
	{ "mtqp",      1038 },
	{ "mupdate",   3905 },
	{ "news",      2009 },
	{ "nfs",       2049 },
	{ "nntp",      119 },
	{ "rtsp",      554 },
	{ "sip",       5060 },
	{ "snmp",      161 },
	{ "telnet",    23 },
	{ "tftp",      69 },
	{ "vemmi",     575 },
	{ "afs",       1483 },
	{ "jms",       5673 },
	{ "rsync",     873 },
	{ "prospero",  191 },
	{ "videotex",  51 },
};

struct medusa_url * medusa_url_parse (const char *uri)
{
        char *i;
        char *s;
        char *p;
        char *e;
        char *t;

        int rs;
        unsigned int j;
        struct medusa_url *url;

        rs = -EIO;
        url = NULL;

        if (uri == NULL) {
                rs = -EINVAL;
                goto bail;
        }

        url = malloc(sizeof(struct medusa_url));
        if (url == NULL) {
                rs = -ENOMEM;
                goto bail;
        }
        memset(url, 0, sizeof(struct medusa_url));

        url->base = strdup(uri);
        if (url->base == NULL) {
                rs = -ENOMEM;
                goto bail;
        }

        if (url->base[0] == '<') {
                memmove(url->base, url->base + 1, strlen(url->base) - 1);
                t = strchr(url->base, '>');
                if (t != NULL) {
                        *t = '\0';
                }
        }

        i = url->base;

        s = strstr(i, "://");
        e = strchr(i, '/');
        if (s == NULL || e < s) {
                url->scheme = NULL;
        } else {
                url->scheme = i;
                *(e - 1) = '\0';
                i = s + 3;

                for (j = 0; j < sizeof(g_services) / sizeof(g_services[0]); j++) {
                        if (strcasecmp(g_services[j].scheme, url->scheme) == 0) {
                                url->port = g_services[j].port;
                        }
                }
        }

        p = strchr(i, ':');
        e = strchr(i, '/');
        if (p != NULL && (e == NULL || e > p)) {
                url->port = atoi(p + 1);
                *p = '\0';
        }
        url->host = i;
        if (e != NULL) {
                *e = '\0';
        }

        if (e != NULL) {
                do {
                        e++;
                } while (*e == '/');
                url->path = e;
        }

        if (url->host == NULL) {
                rs = -EINVAL;
                goto bail;
        }

        return url;
bail:   if (url != NULL) {
                medusa_url_destroy(url);
        }
        return MEDUSA_ERR_PTR(rs);
}

void medusa_url_destroy (struct medusa_url *url)
{
        if (url == NULL) {
                return;
        }
        if (url->base != NULL) {
                free(url->base);
        }
        free(url);
}

const char * medusa_url_get_scheme (struct medusa_url *url)
{
        if (url == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return url->scheme;
}

const char * medusa_url_get_host (struct medusa_url *url)
{
        if (url == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return url->host;
}

int medusa_url_get_port (struct medusa_url *url)
{
        if (url == NULL) {
                return -EINVAL;
        }
        return url->port;
}

const char * medusa_url_get_path (struct medusa_url *url)
{
        if (url == NULL) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return url->path;
}
