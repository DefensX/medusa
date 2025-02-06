
#if !defined(MEDUSA_WEBSOCKETSERVER_STRUCT_H)
#define MEDUSA_WEBSOCKETSERVER_STRUCT_H

TAILQ_HEAD(medusa_websocketserver_clients, medusa_websocketserver_client);
struct medusa_websocketserver_client {
        struct medusa_subject subject;
        unsigned int state;
        unsigned int flags;
        int error;
        int (*onevent) (struct medusa_websocketserver_client *websocketserver_client, unsigned int events, void *context, void *param);
        void *context;
        void *userdata;
        struct medusa_tcpsocket *tcpsocket;
        http_parser http_parser;
        http_parser_settings http_parser_settings;
        char *http_parser_header_field;
        char *http_parser_header_value;
        char *sec_websocket_key;
        char *sec_websocket_protocol;
        unsigned int frame_state;
        unsigned int frame_mask_offset;
        unsigned int frame_payload_offset;
        unsigned int frame_payload_length;
        struct medusa_websocketserver *websocketserver;
        TAILQ_ENTRY(medusa_websocketserver_client) list;
};

struct medusa_websocketserver {
        struct medusa_subject subject;
        unsigned int state;
        unsigned int flags;
        unsigned int error;
        int (*onevent) (struct medusa_websocketserver *websocketserver, unsigned int events, void *context, void *param);
        void *context;
        void *userdata;
        unsigned int protocol;
        char *address;
        unsigned short port;
        char *servername;
        int reuseport;
        int backlog;
        int buffered;
        struct medusa_tcpsocket *tcpsocket;
        struct medusa_websocketserver_clients clients;
};

#endif
