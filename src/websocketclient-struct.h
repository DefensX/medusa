
#if !defined(MEDUSA_WEBSOCKETCLIENT_STRUCT_H)
#define MEDUSA_WEBSOCKETCLIENT_STRUCT_H

struct medusa_websocketclient {
        struct medusa_subject subject;
        unsigned int state;
        unsigned int flags;
        int error;
        int (*onevent) (struct medusa_websocketclient *websocketclient, unsigned int events, void *context, void *param);
        void *context;
        void *userdata;
        struct medusa_tcpsocket *tcpsocket;
        http_parser http_parser;
        http_parser_settings http_parser_settings;
        char *http_parser_header_field;
        char *http_parser_header_value;
        char *sec_websocket_path;
        char *sec_websocket_protocol;
        char *sec_websocket_key;
        char *sec_websocket_accept;
        unsigned int frame_state;
        unsigned int frame_mask_offset;
        unsigned int frame_payload_offset;
        unsigned int frame_payload_length;
};

#endif
