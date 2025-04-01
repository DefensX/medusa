
#if !defined(MEDUSA_BUFFER_H)
#define MEDUSA_BUFFER_H

struct medusa_iovec;
struct medusa_buffer;

enum {
        MEDUSA_BUFFER_TYPE_SIMPLE               = 0,
        MEDUSA_BUFFER_TYPE_RING                 = 1,
        MEDUSA_BUFFER_TYPE_DEFAULT              = MEDUSA_BUFFER_TYPE_RING
#define MEDUSA_BUFFER_TYPE_SIMPLE               MEDUSA_BUFFER_TYPE_SIMPLE
#define MEDUSA_BUFFER_TYPE_RING                 MEDUSA_BUFFER_TYPE_RING
#define MEDUSA_BUFFER_TYPE_DEFAULT              MEDUSA_BUFFER_TYPE_DEFAULT
};

enum {
        MEDUSA_BUFFER_FLAG_NONE                 = (1 <<  0), /* 0x00000001 */
        MEDUSA_BUFFER_FLAG_THREAD_SAFE          = (1 <<  1), /* 0x00000002 */
        MEDUSA_BUFFER_FLAG_DEFAULT              = MEDUSA_BUFFER_FLAG_THREAD_SAFE,
#define MEDUSA_BUFFER_FLAG_NONE                 MEDUSA_BUFFER_FLAG_NONE
#define MEDUSA_BUFFER_FLAG_THREAD_SAFE          MEDUSA_BUFFER_FLAG_THREAD_SAFE
#define MEDUSA_BUFFER_FLAG_DEFAULT              MEDUSA_BUFFER_FLAG_DEFAULT
};

enum {
        MEDUSA_BUFFER_EVENT_WRITE               = (1 <<  0), /* 0x00000001 */
        MEDUSA_BUFFER_EVENT_CHOKE               = (1 <<  1), /* 0x00000002 */
        MEDUSA_BUFFER_EVENT_DESTROY             = (1 <<  2), /* 0x00000004 */
#define MEDUSA_BUFFER_EVENT_WRITE               MEDUSA_BUFFER_EVENT_WRITE
#define MEDUSA_BUFFER_EVENT_CHOKE               MEDUSA_BUFFER_EVENT_CHOKE
#define MEDUSA_BUFFER_EVENT_DESTROY             MEDUSA_BUFFER_EVENT_DESTROY
};

#define MEDUSA_BUFFER_DEFAULT_GROW_SIZE         1024

struct medusa_buffer_init_options {
        unsigned int type;
        unsigned int flags;
        unsigned int grow_size;
        int (*onevent) (struct medusa_buffer *buffer, unsigned int events, void *context, void *param);
        void *context;
};

#ifdef __cplusplus
extern "C"
{
#endif

int medusa_buffer_init_options_default (struct medusa_buffer_init_options *options);

struct medusa_buffer * medusa_buffer_create (unsigned int type);
struct medusa_buffer * medusa_buffer_create_with_options (const struct medusa_buffer_init_options *options);
void medusa_buffer_destroy (struct medusa_buffer *buffer);

int medusa_buffer_reset (struct medusa_buffer *buffer);

int64_t medusa_buffer_get_size   (const struct medusa_buffer *buffer);
int64_t medusa_buffer_get_length (const struct medusa_buffer *buffer);

int64_t medusa_buffer_prepend  (struct medusa_buffer *buffer, const void *data, int64_t length);
int64_t medusa_buffer_prependv (struct medusa_buffer *buffer, const struct medusa_iovec *iovecs, int64_t niovecs);
int64_t medusa_buffer_append   (struct medusa_buffer *buffer, const void *data, int64_t length);
int64_t medusa_buffer_appendv  (struct medusa_buffer *buffer, const struct medusa_iovec *iovecs, int64_t niovecs);
int64_t medusa_buffer_insert   (struct medusa_buffer *buffer, int64_t offset, const void *data, int64_t length);
int64_t medusa_buffer_insertv  (struct medusa_buffer *buffer, int64_t offset, const struct medusa_iovec *iovecs, int64_t niovecs);

int64_t medusa_buffer_prependf  (struct medusa_buffer *buffer, const char *format, ...)  __attribute__((format(printf, 2, 3)));
int64_t medusa_buffer_prependfv (struct medusa_buffer *buffer, const char *format, va_list va);
int64_t medusa_buffer_appendf   (struct medusa_buffer *buffer, const char *format, ...)  __attribute__((format(printf, 2, 3)));
int64_t medusa_buffer_appendfv  (struct medusa_buffer *buffer, const char *format, va_list va);
int64_t medusa_buffer_insertf   (struct medusa_buffer *buffer, int64_t offset, const char *format, ...)  __attribute__((format(printf, 3, 4)));
int64_t medusa_buffer_insertfv  (struct medusa_buffer *buffer, int64_t offset, const char *format, va_list va);

int64_t medusa_buffer_prepend_uint8     (struct medusa_buffer *buffer, uint8_t value);
int64_t medusa_buffer_prepend_uint8_le  (struct medusa_buffer *buffer, uint8_t value);
int64_t medusa_buffer_prepend_uint8_be  (struct medusa_buffer *buffer, uint8_t value);
int64_t medusa_buffer_prepend_uint16    (struct medusa_buffer *buffer, uint16_t value);
int64_t medusa_buffer_prepend_uint16_le (struct medusa_buffer *buffer, uint16_t value);
int64_t medusa_buffer_prepend_uint16_be (struct medusa_buffer *buffer, uint16_t value);
int64_t medusa_buffer_prepend_uint32    (struct medusa_buffer *buffer, uint32_t value);
int64_t medusa_buffer_prepend_uint32_le (struct medusa_buffer *buffer, uint32_t value);
int64_t medusa_buffer_prepend_uint32_be (struct medusa_buffer *buffer, uint32_t value);
int64_t medusa_buffer_prepend_uint64    (struct medusa_buffer *buffer, uint64_t value);
int64_t medusa_buffer_prepend_uint64_le (struct medusa_buffer *buffer, uint64_t value);
int64_t medusa_buffer_prepend_uint64_be (struct medusa_buffer *buffer, uint64_t value);

int64_t medusa_buffer_append_uint8     (struct medusa_buffer *buffer, uint8_t value);
int64_t medusa_buffer_append_uint8_le  (struct medusa_buffer *buffer, uint8_t value);
int64_t medusa_buffer_append_uint8_be  (struct medusa_buffer *buffer, uint8_t value);
int64_t medusa_buffer_append_uint16    (struct medusa_buffer *buffer, uint16_t value);
int64_t medusa_buffer_append_uint16_le (struct medusa_buffer *buffer, uint16_t value);
int64_t medusa_buffer_append_uint16_be (struct medusa_buffer *buffer, uint16_t value);
int64_t medusa_buffer_append_uint32    (struct medusa_buffer *buffer, uint32_t value);
int64_t medusa_buffer_append_uint32_le (struct medusa_buffer *buffer, uint32_t value);
int64_t medusa_buffer_append_uint32_be (struct medusa_buffer *buffer, uint32_t value);
int64_t medusa_buffer_append_uint64    (struct medusa_buffer *buffer, uint64_t value);
int64_t medusa_buffer_append_uint64_le (struct medusa_buffer *buffer, uint64_t value);
int64_t medusa_buffer_append_uint64_be (struct medusa_buffer *buffer, uint64_t value);

int64_t medusa_buffer_insert_uint8     (struct medusa_buffer *buffer, int64_t offset, uint8_t value);
int64_t medusa_buffer_insert_uint8_le  (struct medusa_buffer *buffer, int64_t offset, uint8_t value);
int64_t medusa_buffer_insert_uint8_be  (struct medusa_buffer *buffer, int64_t offset, uint8_t value);
int64_t medusa_buffer_insert_uint16    (struct medusa_buffer *buffer, int64_t offset, uint16_t value);
int64_t medusa_buffer_insert_uint16_le (struct medusa_buffer *buffer, int64_t offset, uint16_t value);
int64_t medusa_buffer_insert_uint16_be (struct medusa_buffer *buffer, int64_t offset, uint16_t value);
int64_t medusa_buffer_insert_uint32    (struct medusa_buffer *buffer, int64_t offset, uint32_t value);
int64_t medusa_buffer_insert_uint32_le (struct medusa_buffer *buffer, int64_t offset, uint32_t value);
int64_t medusa_buffer_insert_uint32_be (struct medusa_buffer *buffer, int64_t offset, uint32_t value);
int64_t medusa_buffer_insert_uint64    (struct medusa_buffer *buffer, int64_t offset, uint64_t value);
int64_t medusa_buffer_insert_uint64_le (struct medusa_buffer *buffer, int64_t offset, uint64_t value);
int64_t medusa_buffer_insert_uint64_be (struct medusa_buffer *buffer, int64_t offset, uint64_t value);

int64_t medusa_buffer_printf  (struct medusa_buffer *buffer, const char *format, ...)  __attribute__((format(printf, 2, 3)));
int64_t medusa_buffer_vprintf (struct medusa_buffer *buffer, const char *format, va_list va);

int64_t medusa_buffer_reservev (struct medusa_buffer *buffer, int64_t length, struct medusa_iovec *iovecs, int64_t niovecs);
int64_t medusa_buffer_commitv  (struct medusa_buffer *buffer, const struct medusa_iovec *iovecs, int64_t niovecs);

int64_t medusa_buffer_peekv (const struct medusa_buffer *buffer, int64_t offset, int64_t length, struct medusa_iovec *iovecs, int64_t niovecs);
int64_t medusa_buffer_choke (struct medusa_buffer *buffer, int64_t offset, int64_t length);

void * medusa_buffer_linearize (struct medusa_buffer *buffer, int64_t offset, int64_t length);

int medusa_buffer_memcmp (const struct medusa_buffer *buffer, int64_t offset, const void *data, int64_t length);
int64_t medusa_buffer_memmem (const struct medusa_buffer *buffer, int64_t offset, const void *data, int64_t length);

int medusa_buffer_strcmp (const struct medusa_buffer *buffer, int64_t offset, const char *str);
int medusa_buffer_strncmp (const struct medusa_buffer *buffer, int64_t offset, const char *str, int64_t n);
int medusa_buffer_strcasecmp (const struct medusa_buffer *buffer, int64_t offset, const char *str);
int medusa_buffer_strncasecmp (const struct medusa_buffer *buffer, int64_t offset, const char *str, int64_t n);
int64_t medusa_buffer_strchr (const struct medusa_buffer *buffer, int64_t offset, const char chr);
int64_t medusa_buffer_strcasechr (const struct medusa_buffer *buffer, int64_t offset, const char chr);
int64_t medusa_buffer_strstr (const struct medusa_buffer *buffer, int64_t offset, const char *str);
int64_t medusa_buffer_strcasestr (const struct medusa_buffer *buffer, int64_t offset, const char *str);

int64_t medusa_buffer_peek  (const struct medusa_buffer *buffer, void *data, int64_t length);
int64_t medusa_buffer_read  (struct medusa_buffer *buffer, void *data, int64_t length);
int64_t medusa_buffer_write (struct medusa_buffer *buffer, const void *data, int64_t length);
int64_t medusa_buffer_writev (struct medusa_buffer *buffer, const struct medusa_iovec *iovecs, int64_t niovecs);

int medusa_buffer_peek_data      (const struct medusa_buffer *buffer, int64_t offset, void *data, int64_t length);
int medusa_buffer_peek_uint8     (const struct medusa_buffer *buffer, int64_t offset, uint8_t *value);
int medusa_buffer_peek_uint8_le  (const struct medusa_buffer *buffer, int64_t offset, uint8_t *value);
int medusa_buffer_peek_uint8_be  (const struct medusa_buffer *buffer, int64_t offset, uint8_t *value);
int medusa_buffer_peek_uint16    (const struct medusa_buffer *buffer, int64_t offset, uint16_t *value);
int medusa_buffer_peek_uint16_le (const struct medusa_buffer *buffer, int64_t offset, uint16_t *value);
int medusa_buffer_peek_uint16_be (const struct medusa_buffer *buffer, int64_t offset, uint16_t *value);
int medusa_buffer_peek_uint32    (const struct medusa_buffer *buffer, int64_t offset, uint32_t *value);
int medusa_buffer_peek_uint32_le (const struct medusa_buffer *buffer, int64_t offset, uint32_t *value);
int medusa_buffer_peek_uint32_be (const struct medusa_buffer *buffer, int64_t offset, uint32_t *value);
int medusa_buffer_peek_uint64    (const struct medusa_buffer *buffer, int64_t offset, uint64_t *value);
int medusa_buffer_peek_uint64_le (const struct medusa_buffer *buffer, int64_t offset, uint64_t *value);
int medusa_buffer_peek_uint64_be (const struct medusa_buffer *buffer, int64_t offset, uint64_t *value);

int medusa_buffer_read_data      (struct medusa_buffer *buffer, int64_t offset, void *data, int64_t length);
int medusa_buffer_read_uint8     (struct medusa_buffer *buffer, int64_t offset, uint8_t *value);
int medusa_buffer_read_uint8_le  (struct medusa_buffer *buffer, int64_t offset, uint8_t *value);
int medusa_buffer_read_uint8_be  (struct medusa_buffer *buffer, int64_t offset, uint8_t *value);
int medusa_buffer_read_uint16    (struct medusa_buffer *buffer, int64_t offset, uint16_t *value);
int medusa_buffer_read_uint16_le (struct medusa_buffer *buffer, int64_t offset, uint16_t *value);
int medusa_buffer_read_uint16_be (struct medusa_buffer *buffer, int64_t offset, uint16_t *value);
int medusa_buffer_read_uint32    (struct medusa_buffer *buffer, int64_t offset, uint32_t *value);
int medusa_buffer_read_uint32_le (struct medusa_buffer *buffer, int64_t offset, uint32_t *value);
int medusa_buffer_read_uint32_be (struct medusa_buffer *buffer, int64_t offset, uint32_t *value);
int medusa_buffer_read_uint64    (struct medusa_buffer *buffer, int64_t offset, uint64_t *value);
int medusa_buffer_read_uint64_le (struct medusa_buffer *buffer, int64_t offset, uint64_t *value);
int medusa_buffer_read_uint64_be (struct medusa_buffer *buffer, int64_t offset, uint64_t *value);

const char * medusa_buffer_event_string (unsigned int events);

#ifdef __cplusplus
}
#endif

#endif
