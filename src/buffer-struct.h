
#if !defined(MEDUSA_BUFFER_STRUCT_H)
#define MEDUSA_BUFFER_STRUCT_H

struct iovec;
struct medusa_buffer;

struct medusa_buffer_backend {
        int64_t (*get_size) (const struct medusa_buffer *buffer);
        int64_t (*get_length) (const struct medusa_buffer *buffer);

        int (*prependv) (struct medusa_buffer *buffer, const struct iovec *iovecs, int niovecs);
        int (*appendv) (struct medusa_buffer *buffer, const struct iovec *iovecs, int niovecs);
        int (*vprintf) (struct medusa_buffer *buffer, const char *format, va_list va);

        int (*reserve) (struct medusa_buffer *buffer, int64_t length, struct iovec *iovecs, int niovecs);
        int (*commit) (struct medusa_buffer *buffer, const struct iovec *iovecs, int niovecs);

        int (*peek) (struct medusa_buffer *buffer, int64_t offset, int64_t length, struct iovec *iovecs, int niovecs);

        int (*choke) (struct medusa_buffer *buffer, int64_t length);

        int (*reset) (struct medusa_buffer *buffer);

        void (*destroy) (struct medusa_buffer *buffer);
};

struct medusa_buffer {
        const struct medusa_buffer_backend *backend;
};

#endif
