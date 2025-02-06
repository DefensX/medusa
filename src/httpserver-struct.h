
#if !defined(MEDUSA_HTTPSERVER_STRUCT_H)
#define MEDUSA_HTTPSERVER_STRUCT_H

TAILQ_HEAD(medusa_httpserver_clients, medusa_httpserver_client);
struct medusa_httpserver_client {
        struct medusa_subject subject;
        unsigned int state;
        unsigned int flags;
        unsigned int error;
        double read_timeout;
        double write_timeout;
        int (*onevent) (struct medusa_httpserver_client *httpserver_client, unsigned int events, void *context, void *param);
        void *context;
        void *userdata;
        struct medusa_tcpsocket *tcpsocket;
        http_parser http_parser;
        http_parser_settings http_parser_settings;
        struct medusa_httpserver_client_request *request;
        struct medusa_httpserver *httpserver;
        TAILQ_ENTRY(medusa_httpserver_client) list;
};

struct medusa_httpserver {
        struct medusa_subject subject;
        unsigned int state;
        unsigned int flags;
        unsigned int error;
        int (*onevent) (struct medusa_httpserver *httpserver, unsigned int events, void *context, void *param);
        void *context;
        void *userdata;
        unsigned int protocol;
        char *address;
        unsigned short port;
        int backlog;
        struct medusa_tcpsocket *tcpsocket;
        struct medusa_httpserver_clients clients;
};

#endif
