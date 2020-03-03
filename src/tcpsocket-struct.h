
#if !defined(MEDUSA_TCPSOCKET_STRUCT_H)
#define MEDUSA_TCPSOCKET_STRUCT_H

struct medusa_tcpsocket {
        struct medusa_subject subject;
        int (*onevent) (struct medusa_tcpsocket *tcpsocket, unsigned int events, void *context, void *param);
        void *context;
        unsigned int flags;
        unsigned int state;
        unsigned int error;
        int backlog;
        struct medusa_io *io;
        struct medusa_timer *ctimer;
        struct medusa_timer *rtimer;
        struct medusa_buffer *wbuffer;
        struct medusa_buffer *rbuffer;
#if defined(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) && (MEDUSA_TCPSOCKET_OPENSSL_ENABLE == 1)
        SSL *ssl;
        SSL_CTX *ssl_context;
        char *ssl_certificate;
        char *ssl_privatekey;
        int ssl_wantread;
        int ssl_wantwrite;
#endif
        void *userdata;
};

#endif
