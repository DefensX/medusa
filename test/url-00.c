
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <medusa/error.h>
#include <medusa/url.h>

static int __strcasecmp (const char *s1, const char *s2)
{
        if (s1 == NULL && s2 != NULL) {
                return -1;
        }
        if (s1 != NULL && s2 == NULL) {
                return 1;
        }
        if (s1 == NULL && s2 == NULL) {
                return 0;
        }
        return strcasecmp(s1, s2);
}

static const struct {
        const char *url;
        const char *scheme;
        const char *host;
        unsigned short port;
        const char *path;
        const char *username;
        const char *password;
} g_tests[] = {
        { "", NULL, NULL, 0, NULL, NULL, NULL },
        { "/", NULL, NULL, 0, "", NULL, NULL },
        { "//", NULL, NULL, 0, "/", NULL, NULL },
        { "/a", NULL, NULL, 0, "a", NULL, NULL },
        { "/a/", NULL, NULL, 0, "a/", NULL, NULL },
        { "/a//", NULL, NULL, 0, "a//", NULL, NULL },
        { "/a/b", NULL, NULL, 0, "a/b", NULL, NULL },
        { "/a/b/", NULL, NULL, 0, "a/b/", NULL, NULL },
        { "/a/b//", NULL, NULL, 0, "a/b//", NULL, NULL },
        { ":1", NULL, NULL, 1, NULL, NULL, NULL },
        { ":1/", NULL, NULL, 1, "", NULL, NULL },
        { ":1//", NULL, NULL, 1, "/", NULL, NULL },
        { ":1/a", NULL, NULL, 1, "a", NULL, NULL },
        { ":1/a/", NULL, NULL, 1, "a/", NULL, NULL },
        { ":1/a//", NULL, NULL, 1, "a//", NULL, NULL },
        { ":1/a/b", NULL, NULL, 1, "a/b", NULL, NULL },
        { ":1/a/b/", NULL, NULL, 1, "a/b/", NULL, NULL },
        { ":1/a/b//", NULL, NULL, 1, "a/b//", NULL, NULL },
        { "/:1", NULL, NULL, 0, ":1", NULL, NULL },
        { "//:1", NULL, NULL, 0, "/:1", NULL, NULL },
        { "/a:1", NULL, NULL, 0, "a:1", NULL, NULL },
        { "/a/:1", NULL, NULL, 0, "a/:1", NULL, NULL },
        { "/a//:1", NULL, NULL, 0, "a//:1", NULL, NULL },
        { "/a/b:1", NULL, NULL, 0, "a/b:1", NULL, NULL },
        { "/a/b/:1", NULL, NULL, 0, "a/b/:1", NULL, NULL },
        { "/a/b//:1", NULL, NULL, 0, "a/b//:1", NULL, NULL },
        { "/http://", NULL, NULL, 0, "http://", NULL, NULL },
        { "//http://", NULL, NULL, 0, "/http://", NULL, NULL },
        { "/ahttp://", NULL, NULL, 0, "ahttp://", NULL, NULL },
        { "/a/http://", NULL, NULL, 0, "a/http://", NULL, NULL },
        { "/a//http://", NULL, NULL, 0, "a//http://", NULL, NULL },
        { "/a/bhttp://", NULL, NULL, 0, "a/bhttp://", NULL, NULL },
        { "/a/b/http://", NULL, NULL, 0, "a/b/http://", NULL, NULL },
        { "/a/b//http://", NULL, NULL, 0, "a/b//http://", NULL, NULL },
        { "a.com", NULL, "a.com", 0, NULL, NULL, NULL },
        { "a.com/", NULL, "a.com", 0, "", NULL, NULL },
        { "a.com//", NULL, "a.com", 0, "/", NULL, NULL },
        { "a.com/a", NULL, "a.com", 0, "a", NULL, NULL },
        { "a.com/a/", NULL, "a.com", 0, "a/", NULL, NULL },
        { "a.com/a//", NULL, "a.com", 0, "a//", NULL, NULL },
        { "a.com/a/b", NULL, "a.com", 0, "a/b", NULL, NULL },
        { "a.com/a/b/", NULL, "a.com", 0, "a/b/", NULL, NULL },
        { "a.com/a/b//", NULL, "a.com", 0, "a/b//", NULL, NULL },
        { "a.com:1", NULL, "a.com", 1, NULL, NULL, NULL },
        { "a.com:1/", NULL, "a.com", 1, "", NULL, NULL },
        { "a.com:1//", NULL, "a.com", 1, "/", NULL, NULL },
        { "a.com:1/a", NULL, "a.com", 1, "a", NULL, NULL },
        { "a.com:1/a/", NULL, "a.com", 1, "a/", NULL, NULL },
        { "a.com:1/a//", NULL, "a.com", 1, "a//", NULL, NULL },
        { "a.com:1/a/b", NULL, "a.com", 1, "a/b", NULL, NULL },
        { "a.com:1/a/b/", NULL, "a.com", 1, "a/b/", NULL, NULL },
        { "a.com:1/a/b//", NULL, "a.com", 1, "a/b//", NULL, NULL },
        { "a.com/:1", NULL, "a.com", 0, ":1", NULL, NULL },
        { "a.com//:1", NULL, "a.com", 0, "/:1", NULL, NULL },
        { "a.com/a:1", NULL, "a.com", 0, "a:1", NULL, NULL },
        { "a.com/a/:1", NULL, "a.com", 0, "a/:1", NULL, NULL },
        { "a.com/a//:1", NULL, "a.com", 0, "a//:1", NULL, NULL },
        { "a.com/a/b:1", NULL, "a.com", 0, "a/b:1", NULL, NULL },
        { "a.com/a/b/:1", NULL, "a.com", 0, "a/b/:1", NULL, NULL },
        { "a.com/a/b//:1", NULL, "a.com", 0, "a/b//:1", NULL, NULL },
        { "a.com/http://", NULL, "a.com", 0, "http://", NULL, NULL },
        { "a.com//http://", NULL, "a.com", 0, "/http://", NULL, NULL },
        { "a.com/ahttp://", NULL, "a.com", 0, "ahttp://", NULL, NULL },
        { "a.com/a/http://", NULL, "a.com", 0, "a/http://", NULL, NULL },
        { "a.com/a//http://", NULL, "a.com", 0, "a//http://", NULL, NULL },
        { "a.com/a/bhttp://", NULL, "a.com", 0, "a/bhttp://", NULL, NULL },
        { "a.com/a/b/http://", NULL, "a.com", 0, "a/b/http://", NULL, NULL },
        { "a.com/a/b//http://", NULL, "a.com", 0, "a/b//http://", NULL, NULL },
        { "http://", "http", NULL, 80, NULL, NULL, NULL },
        { "http://a.com", "http", "a.com", 80, NULL, NULL, NULL },
        { "http://a.com/", "http", "a.com", 80, "", NULL, NULL },
        { "http://a.com//", "http", "a.com", 80, "/", NULL, NULL },
        { "http://a.com/a", "http", "a.com", 80, "a", NULL, NULL },
        { "http://a.com/a/", "http", "a.com", 80, "a/", NULL, NULL },
        { "http://a.com/a//", "http", "a.com", 80, "a//", NULL, NULL },
        { "http://a.com/a/b", "http", "a.com", 80, "a/b", NULL, NULL },
        { "http://a.com/a/b/", "http", "a.com", 80, "a/b/", NULL, NULL },
        { "http://a.com/a/b//", "http", "a.com", 80, "a/b//", NULL, NULL },
        { "http://a.com:1", "http", "a.com", 1, NULL, NULL, NULL },
        { "http://a.com:1/", "http", "a.com", 1, "", NULL, NULL },
        { "http://a.com:1//", "http", "a.com", 1, "/", NULL, NULL },
        { "http://a.com:1/a", "http", "a.com", 1, "a", NULL, NULL },
        { "http://a.com:1/a/", "http", "a.com", 1, "a/", NULL, NULL },
        { "http://a.com:1/a//", "http", "a.com", 1, "a//", NULL, NULL },
        { "http://a.com:1/a/b", "http", "a.com", 1, "a/b", NULL, NULL },
        { "http://a.com:1/a/b/", "http", "a.com", 1, "a/b/", NULL, NULL },
        { "http://a.com:1/a/b//", "http", "a.com", 1, "a/b//", NULL, NULL },
        { "http://a.com/:1", "http", "a.com", 80, ":1", NULL, NULL },
        { "http://a.com//:1", "http", "a.com", 80, "/:1", NULL, NULL },
        { "http://a.com/a:1", "http", "a.com", 80, "a:1", NULL, NULL },
        { "http://a.com/a/:1", "http", "a.com", 80, "a/:1", NULL, NULL },
        { "http://a.com/a//:1", "http", "a.com", 80, "a//:1", NULL, NULL },
        { "http://a.com/a/b:1", "http", "a.com", 80, "a/b:1", NULL, NULL },
        { "http://a.com/a/b/:1", "http", "a.com", 80, "a/b/:1", NULL, NULL },
        { "http://a.com/a/b//:1", "http", "a.com", 80, "a/b//:1", NULL, NULL },
        { "<>", NULL, NULL, 0, NULL, NULL, NULL },
        { "<a.com>", NULL, "a.com", 0, NULL, NULL, NULL },
        { "<a.com/>", NULL, "a.com", 0, "", NULL, NULL },
        { "<a.com//>", NULL, "a.com", 0, "/", NULL, NULL },
        { "<a.com/a>", NULL, "a.com", 0, "a", NULL, NULL },
        { "<a.com/a/>", NULL, "a.com", 0, "a/", NULL, NULL },
        { "<a.com/a//>", NULL, "a.com", 0, "a//", NULL, NULL },
        { "<a.com/a/b>", NULL, "a.com", 0, "a/b", NULL, NULL },
        { "<a.com/a/b/>", NULL, "a.com", 0, "a/b/", NULL, NULL },
        { "<a.com/a/b//>", NULL, "a.com", 0, "a/b//", NULL, NULL },
        { "<a.com:1>", NULL, "a.com", 1, NULL, NULL, NULL },
        { "<a.com:1/>", NULL, "a.com", 1, "", NULL, NULL },
        { "<a.com:1//>", NULL, "a.com", 1, "/", NULL, NULL },
        { "<a.com:1/a>", NULL, "a.com", 1, "a", NULL, NULL },
        { "<a.com:1/a/>", NULL, "a.com", 1, "a/", NULL, NULL },
        { "<a.com:1/a//>", NULL, "a.com", 1, "a//", NULL, NULL },
        { "<a.com:1/a/b>", NULL, "a.com", 1, "a/b", NULL, NULL },
        { "<a.com:1/a/b/>", NULL, "a.com", 1, "a/b/", NULL, NULL },
        { "<a.com:1/a/b//>", NULL, "a.com", 1, "a/b//", NULL, NULL },
        { "<a.com/:1>", NULL, "a.com", 0, ":1", NULL, NULL },
        { "<a.com//:1>", NULL, "a.com", 0, "/:1", NULL, NULL },
        { "<a.com/a:1>", NULL, "a.com", 0, "a:1", NULL, NULL },
        { "<a.com/a/:1>", NULL, "a.com", 0, "a/:1", NULL, NULL },
        { "<a.com/a//:1>", NULL, "a.com", 0, "a//:1", NULL, NULL },
        { "<a.com/a/b:1>", NULL, "a.com", 0, "a/b:1", NULL, NULL },
        { "<a.com/a/b/:1>", NULL, "a.com", 0, "a/b/:1", NULL, NULL },
        { "<a.com/a/b//:1>", NULL, "a.com", 0, "a/b//:1", NULL, NULL },
        { "<a.com/http://>", NULL, "a.com", 0, "http://", NULL, NULL },
        { "<a.com//http://>", NULL, "a.com", 0, "/http://", NULL, NULL },
        { "<a.com/ahttp://>", NULL, "a.com", 0, "ahttp://", NULL, NULL },
        { "<a.com/a/http://>", NULL, "a.com", 0, "a/http://", NULL, NULL },
        { "<a.com/a//http://>", NULL, "a.com", 0, "a//http://", NULL, NULL },
        { "<a.com/a/bhttp://>", NULL, "a.com", 0, "a/bhttp://", NULL, NULL },
        { "<a.com/a/b/http://>", NULL, "a.com", 0, "a/b/http://", NULL, NULL },
        { "<a.com/a/b//http://>", NULL, "a.com", 0, "a/b//http://", NULL, NULL },
        { "<http://>", "http", NULL, 80, NULL, NULL, NULL},
        { "<http://a.com>", "http", "a.com", 80, NULL, NULL, NULL },
        { "<http://a.com/>", "http", "a.com", 80, "", NULL, NULL },
        { "<http://a.com//>", "http", "a.com", 80, "/", NULL, NULL },
        { "<http://a.com/a>", "http", "a.com", 80, "a", NULL, NULL },
        { "<http://a.com/a/>", "http", "a.com", 80, "a/", NULL, NULL },
        { "<http://a.com/a//>", "http", "a.com", 80, "a//", NULL, NULL },
        { "<http://a.com/a/b>", "http", "a.com", 80, "a/b", NULL, NULL },
        { "<http://a.com/a/b/>", "http", "a.com", 80, "a/b/", NULL, NULL },
        { "<http://a.com/a/b//>", "http", "a.com", 80, "a/b//", NULL, NULL },
        { "<http://a.com:1>", "http", "a.com", 1, NULL, NULL, NULL },
        { "<http://a.com:1/>", "http", "a.com", 1, "", NULL, NULL },
        { "<http://a.com:1//>", "http", "a.com", 1, "/", NULL, NULL },
        { "<http://a.com:1/a>", "http", "a.com", 1, "a", NULL, NULL },
        { "<http://a.com:1/a/>", "http", "a.com", 1, "a/", NULL, NULL },
        { "<http://a.com:1/a//>", "http", "a.com", 1, "a//", NULL, NULL },
        { "<http://a.com:1/a/b>", "http", "a.com", 1, "a/b", NULL, NULL },
        { "<http://a.com:1/a/b/>", "http", "a.com", 1, "a/b/", NULL, NULL },
        { "<http://a.com:1/a/b//>", "http", "a.com", 1, "a/b//", NULL, NULL },
        { "<http://a.com/:1>", "http", "a.com", 80, ":1", NULL, NULL },
        { "<http://a.com//:1>", "http", "a.com", 80, "/:1", NULL, NULL },
        { "<http://a.com/a:1>", "http", "a.com", 80, "a:1", NULL, NULL },
        { "<http://a.com/a/:1>", "http", "a.com", 80, "a/:1", NULL, NULL },
        { "<http://a.com/a//:1>", "http", "a.com", 80, "a//:1", NULL, NULL },
        { "<http://a.com/a/b:1>", "http", "a.com", 80, "a/b:1", NULL, NULL },
        { "<http://a.com/a/b/:1>", "http", "a.com", 80, "a/b/:1", NULL, NULL },
        { "<http://a.com/a/b//:1>", "http", "a.com", 80, "a/b//:1", NULL, NULL },
        { "u@a.com", NULL, "a.com", 0, NULL, "u", NULL },
        { "u@a.com/", NULL, "a.com", 0, "", "u", NULL },
        { "u@a.com//", NULL, "a.com", 0, "/", "u", NULL },
        { "u@a.com/a", NULL, "a.com", 0, "a", "u", NULL },
        { "u@a.com/a/", NULL, "a.com", 0, "a/", "u", NULL },
        { "u@a.com/a//", NULL, "a.com", 0, "a//", "u", NULL },
        { "u@a.com/a/b", NULL, "a.com", 0, "a/b", "u", NULL },
        { "u@a.com/a/b/", NULL, "a.com", 0, "a/b/", "u", NULL },
        { "u@a.com/a/b//", NULL, "a.com", 0, "a/b//", "u", NULL },
        { "u@a.com:1", NULL, "a.com", 1, NULL, "u", NULL },
        { "u@a.com:1/", NULL, "a.com", 1, "", "u", NULL },
        { "u@a.com:1//", NULL, "a.com", 1, "/", "u", NULL },
        { "u@a.com:1/a", NULL, "a.com", 1, "a", "u", NULL },
        { "u@a.com:1/a/", NULL, "a.com", 1, "a/", "u", NULL },
        { "u@a.com:1/a//", NULL, "a.com", 1, "a//", "u", NULL },
        { "u@a.com:1/a/b", NULL, "a.com", 1, "a/b", "u", NULL },
        { "u@a.com:1/a/b/", NULL, "a.com", 1, "a/b/", "u", NULL },
        { "u@a.com:1/a/b//", NULL, "a.com", 1, "a/b//", "u", NULL },
        { "u@a.com/:1", NULL, "a.com", 0, ":1", "u", NULL },
        { "u@a.com//:1", NULL, "a.com", 0, "/:1", "u", NULL },
        { "u@a.com/a:1", NULL, "a.com", 0, "a:1", "u", NULL },
        { "u@a.com/a/:1", NULL, "a.com", 0, "a/:1", "u", NULL },
        { "u@a.com/a//:1", NULL, "a.com", 0, "a//:1", "u", NULL },
        { "u@a.com/a/b:1", NULL, "a.com", 0, "a/b:1", "u", NULL },
        { "u@a.com/a/b/:1", NULL, "a.com", 0, "a/b/:1", "u", NULL },
        { "u@a.com/a/b//:1", NULL, "a.com", 0, "a/b//:1", "u", NULL },
        { "u@a.com/http://", NULL, "a.com", 0, "http://", "u", NULL },
        { "u@a.com//http://", NULL, "a.com", 0, "/http://", "u", NULL },
        { "u@a.com/ahttp://", NULL, "a.com", 0, "ahttp://", "u", NULL },
        { "u@a.com/a/http://", NULL, "a.com", 0, "a/http://", "u", NULL },
        { "u@a.com/a//http://", NULL, "a.com", 0, "a//http://", "u", NULL },
        { "u@a.com/a/bhttp://", NULL, "a.com", 0, "a/bhttp://", "u", NULL },
        { "u@a.com/a/b/http://", NULL, "a.com", 0, "a/b/http://", "u", NULL },
        { "u@a.com/a/b//http://", NULL, "a.com", 0, "a/b//http://", "u", NULL },
        { "http://u@", "http", NULL, 80, NULL, "u", NULL },
        { "http://u@a.com", "http", "a.com", 80, NULL, "u", NULL },
        { "http://u@a.com/", "http", "a.com", 80, "", "u", NULL },
        { "http://u@a.com//", "http", "a.com", 80, "/", "u", NULL },
        { "http://u@a.com/a", "http", "a.com", 80, "a", "u", NULL },
        { "http://u@a.com/a/", "http", "a.com", 80, "a/", "u", NULL },
        { "http://u@a.com/a//", "http", "a.com", 80, "a//", "u", NULL },
        { "http://u@a.com/a/b", "http", "a.com", 80, "a/b", "u", NULL },
        { "http://u@a.com/a/b/", "http", "a.com", 80, "a/b/", "u", NULL },
        { "http://u@a.com/a/b//", "http", "a.com", 80, "a/b//", "u", NULL },
        { "http://u@a.com:1", "http", "a.com", 1, NULL, "u", NULL },
        { "http://u@a.com:1/", "http", "a.com", 1, "", "u", NULL },
        { "http://u@a.com:1//", "http", "a.com", 1, "/", "u", NULL },
        { "http://u@a.com:1/a", "http", "a.com", 1, "a", "u", NULL },
        { "http://u@a.com:1/a/", "http", "a.com", 1, "a/", "u", NULL },
        { "http://u@a.com:1/a//", "http", "a.com", 1, "a//", "u", NULL },
        { "http://u@a.com:1/a/b", "http", "a.com", 1, "a/b", "u", NULL },
        { "http://u@a.com:1/a/b/", "http", "a.com", 1, "a/b/", "u", NULL },
        { "http://u@a.com:1/a/b//", "http", "a.com", 1, "a/b//", "u", NULL },
        { "http://u@a.com/:1", "http", "a.com", 80, ":1", "u", NULL },
        { "http://u@a.com//:1", "http", "a.com", 80, "/:1", "u", NULL },
        { "http://u@a.com/a:1", "http", "a.com", 80, "a:1", "u", NULL },
        { "http://u@a.com/a/:1", "http", "a.com", 80, "a/:1", "u", NULL },
        { "http://u@a.com/a//:1", "http", "a.com", 80, "a//:1", "u", NULL },
        { "http://u@a.com/a/b:1", "http", "a.com", 80, "a/b:1", "u", NULL },
        { "http://u@a.com/a/b/:1", "http", "a.com", 80, "a/b/:1", "u", NULL },
        { "http://u@a.com/a/b//:1", "http", "a.com", 80, "a/b//:1", "u", NULL },
        { "u:p@a.com", NULL, "a.com", 0, NULL, "u", "p" },
        { "u:p@a.com/", NULL, "a.com", 0, "", "u", "p" },
        { "u:p@a.com//", NULL, "a.com", 0, "/", "u", "p" },
        { "u:p@a.com/a", NULL, "a.com", 0, "a", "u", "p" },
        { "u:p@a.com/a/", NULL, "a.com", 0, "a/", "u", "p" },
        { "u:p@a.com/a//", NULL, "a.com", 0, "a//", "u", "p" },
        { "u:p@a.com/a/b", NULL, "a.com", 0, "a/b", "u", "p" },
        { "u:p@a.com/a/b/", NULL, "a.com", 0, "a/b/", "u", "p" },
        { "u:p@a.com/a/b//", NULL, "a.com", 0, "a/b//", "u", "p" },
        { "u:p@a.com:1", NULL, "a.com", 1, NULL, "u", "p" },
        { "u:p@a.com:1/", NULL, "a.com", 1, "", "u", "p" },
        { "u:p@a.com:1//", NULL, "a.com", 1, "/", "u", "p" },
        { "u:p@a.com:1/a", NULL, "a.com", 1, "a", "u", "p" },
        { "u:p@a.com:1/a/", NULL, "a.com", 1, "a/", "u", "p" },
        { "u:p@a.com:1/a//", NULL, "a.com", 1, "a//", "u", "p" },
        { "u:p@a.com:1/a/b", NULL, "a.com", 1, "a/b", "u", "p" },
        { "u:p@a.com:1/a/b/", NULL, "a.com", 1, "a/b/", "u", "p" },
        { "u:p@a.com:1/a/b//", NULL, "a.com", 1, "a/b//", "u", "p" },
        { "u:p@a.com/:1", NULL, "a.com", 0, ":1", "u", "p" },
        { "u:p@a.com//:1", NULL, "a.com", 0, "/:1", "u", "p" },
        { "u:p@a.com/a:1", NULL, "a.com", 0, "a:1", "u", "p" },
        { "u:p@a.com/a/:1", NULL, "a.com", 0, "a/:1", "u", "p" },
        { "u:p@a.com/a//:1", NULL, "a.com", 0, "a//:1", "u", "p" },
        { "u:p@a.com/a/b:1", NULL, "a.com", 0, "a/b:1", "u", "p" },
        { "u:p@a.com/a/b/:1", NULL, "a.com", 0, "a/b/:1", "u", "p" },
        { "u:p@a.com/a/b//:1", NULL, "a.com", 0, "a/b//:1", "u", "p" },
        { "u:p@a.com/http://", NULL, "a.com", 0, "http://", "u", "p" },
        { "u:p@a.com//http://", NULL, "a.com", 0, "/http://", "u", "p" },
        { "u:p@a.com/ahttp://", NULL, "a.com", 0, "ahttp://", "u", "p" },
        { "u:p@a.com/a/http://", NULL, "a.com", 0, "a/http://", "u", "p" },
        { "u:p@a.com/a//http://", NULL, "a.com", 0, "a//http://", "u", "p" },
        { "u:p@a.com/a/bhttp://", NULL, "a.com", 0, "a/bhttp://", "u", "p" },
        { "u:p@a.com/a/b/http://", NULL, "a.com", 0, "a/b/http://", "u", "p" },
        { "u:p@a.com/a/b//http://", NULL, "a.com", 0, "a/b//http://", "u", "p" },
        { "http://u:p@", "http", NULL, 80, NULL, "u", "p" },
        { "http://u:p@a.com", "http", "a.com", 80, NULL, "u", "p" },
        { "http://u:p@a.com/", "http", "a.com", 80, "", "u", "p" },
        { "http://u:p@a.com//", "http", "a.com", 80, "/", "u", "p" },
        { "http://u:p@a.com/a", "http", "a.com", 80, "a", "u", "p" },
        { "http://u:p@a.com/a/", "http", "a.com", 80, "a/", "u", "p" },
        { "http://u:p@a.com/a//", "http", "a.com", 80, "a//", "u", "p" },
        { "http://u:p@a.com/a/b", "http", "a.com", 80, "a/b", "u", "p" },
        { "http://u:p@a.com/a/b/", "http", "a.com", 80, "a/b/", "u", "p" },
        { "http://u:p@a.com/a/b//", "http", "a.com", 80, "a/b//", "u", "p" },
        { "http://u:p@a.com:1", "http", "a.com", 1, NULL, "u", "p" },
        { "http://u:p@a.com:1/", "http", "a.com", 1, "", "u", "p" },
        { "http://u:p@a.com:1//", "http", "a.com", 1, "/", "u", "p" },
        { "http://u:p@a.com:1/a", "http", "a.com", 1, "a", "u", "p" },
        { "http://u:p@a.com:1/a/", "http", "a.com", 1, "a/", "u", "p" },
        { "http://u:p@a.com:1/a//", "http", "a.com", 1, "a//", "u", "p" },
        { "http://u:p@a.com:1/a/b", "http", "a.com", 1, "a/b", "u", "p" },
        { "http://u:p@a.com:1/a/b/", "http", "a.com", 1, "a/b/", "u", "p" },
        { "http://u:p@a.com:1/a/b//", "http", "a.com", 1, "a/b//", "u", "p" },
        { "http://u:p@a.com/:1", "http", "a.com", 80, ":1", "u", "p" },
        { "http://u:p@a.com//:1", "http", "a.com", 80, "/:1", "u", "p" },
        { "http://u:p@a.com/a:1", "http", "a.com", 80, "a:1", "u", "p" },
        { "http://u:p@a.com/a/:1", "http", "a.com", 80, "a/:1", "u", "p" },
        { "http://u:p@a.com/a//:1", "http", "a.com", 80, "a//:1", "u", "p" },
        { "http://u:p@a.com/a/b:1", "http", "a.com", 80, "a/b:1", "u", "p" },
        { "http://u:p@a.com/a/b/:1", "http", "a.com", 80, "a/b/:1", "u", "p" },
        { "http://u:p@a.com/a/b//:1", "http", "a.com", 80, "a/b//:1", "u", "p" },
};

int main (int argc, char *argv[])
{
        int i;
        struct medusa_url *url;
        (void) argc;
        (void) argv;
        fprintf(stderr, "start\n");
        for (i = 0; i < (int) (sizeof(g_tests) / sizeof(g_tests[0])); i++) {
                fprintf(stderr, "url: '%s'\n", g_tests[i].url);
                url = medusa_url_parse(g_tests[i].url);
                if (MEDUSA_IS_ERR_OR_NULL(url)) {
                        fprintf(stderr, "  fail error: %d\n", MEDUSA_PTR_ERR(url));
                        return -1;
                }
                fprintf(stderr, "  scheme  : %s\n", medusa_url_get_scheme(url));
                fprintf(stderr, "  host    : %s\n", medusa_url_get_host(url));
                fprintf(stderr, "  port    : %d\n", medusa_url_get_port(url));
                fprintf(stderr, "  path    : %s\n", medusa_url_get_path(url));
                fprintf(stderr, "  username: %s\n", medusa_url_get_username(url));
                fprintf(stderr, "  password: %s\n", medusa_url_get_password(url));
                if (__strcasecmp(g_tests[i].scheme, medusa_url_get_scheme(url)) != 0) {
                        fprintf(stderr, "  scheme mismatch: '%s' != '%s'\n", g_tests[i].url, medusa_url_get_scheme(url));
                        return -1;
                }
                if (__strcasecmp(g_tests[i].host, medusa_url_get_host(url))) {
                        fprintf(stderr, "  host mismatch: '%s' != '%s'\n", g_tests[i].host, medusa_url_get_host(url));
                        return -1;
                }
                if (g_tests[i].port != medusa_url_get_port(url)) {
                        fprintf(stderr, "  port mismatch: '%d' != '%d'\n", g_tests[i].port, medusa_url_get_port(url));
                        return -1;
                }
                if (__strcasecmp(g_tests[i].path, medusa_url_get_path(url))) {
                        fprintf(stderr, "  path mismatch: '%s' != '%s'\n", g_tests[i].path, medusa_url_get_path(url));
                        return -1;
                }
                if (__strcasecmp(g_tests[i].username, medusa_url_get_username(url))) {
                        fprintf(stderr, "  username mismatch: '%s' != '%s'\n", g_tests[i].username, medusa_url_get_username(url));
                        return -1;
                }
                if (__strcasecmp(g_tests[i].password, medusa_url_get_password(url))) {
                        fprintf(stderr, "  password mismatch: '%s' != '%s'\n", g_tests[i].password, medusa_url_get_password(url));
                        return -1;
                }
                medusa_url_destroy(url);
        }
        fprintf(stderr, "success\n");
        return 0;
}
