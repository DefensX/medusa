
#if !defined(MEDUSA_BUFFER_STRUCT_H)
#define MEDUSA_BUFFER_STRUCT_H

struct medusa_iovec;
struct medusa_buffer;

struct medusa_buffer_backend {
        int64_t (*get_size) (const struct medusa_buffer *buffer);
        int64_t (*get_length) (const struct medusa_buffer *buffer);

        int64_t (*insertv) (struct medusa_buffer *buffer, int64_t offset, const struct medusa_iovec *iovecs, int64_t niovecs);
        int64_t (*insertfv) (struct medusa_buffer *buffer, int64_t offset, const char *format, va_list va);

        int64_t (*reservev) (struct medusa_buffer *buffer, int64_t length, struct medusa_iovec *iovecs, int64_t niovecs);
        int64_t (*commitv) (struct medusa_buffer *buffer, const struct medusa_iovec *iovecs, int64_t niovecs);

        int64_t (*peekv) (const struct medusa_buffer *buffer, int64_t offset, int64_t length, struct medusa_iovec *iovecs, int64_t niovecs);
        int64_t (*choke) (struct medusa_buffer *buffer, int64_t offset, int64_t length);

        void * (*linearize) (struct medusa_buffer *buffer, int64_t offset, int64_t length);
        int (*shrink) (struct medusa_buffer *buffer, int64_t size);

        int (*reset) (struct medusa_buffer *buffer);
        void (*destroy) (struct medusa_buffer *buffer);
};

struct medusa_buffer {
        const struct medusa_buffer_backend *backend;
        int (*onevent) (struct medusa_buffer *buffer, unsigned int events, void *context, void *param);
        void *context;
};

#endif
