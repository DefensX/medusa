
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include <sys/queue.h>

#include "error.h"
#include "pool.h"
#include "buffer.h"
#include "buffer-struct.h"
#include "buffer-chunked.h"
#include "buffer-chunked-struct.h"

#define MIN(a, b)                               (((a) < (b)) ? (a) : (b))

#define MEDUSA_BUFFER_USE_POOL      1
#if defined(MEDUSA_BUFFER_USE_POOL) && (MEDUSA_BUFFER_USE_POOL == 1)
static struct medusa_pool *g_pool_buffer_chunked;
#endif

static int chunked_buffer_resize (struct medusa_buffer *buffer, int64_t size)
{
        unsigned int i;
        unsigned int c;
        struct medusa_buffer_chunked_entry *entry;
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        if (size < 0) {
                return -EINVAL;
        }
        if (chunked->total_size >= size) {
                return 0;
        }
        c  = size - chunked->total_size;
        c += chunked->chunk_size - 1;
        c /= chunked->chunk_size;
        for (i = 0; i < c; i++) {
                entry = malloc(sizeof(struct medusa_buffer_chunked_entry) + chunked->chunk_size);
                if (entry == NULL) {
                        return -ENOMEM;
                }
                memset(entry, 0, sizeof(struct medusa_buffer_chunked_entry));
                entry->flags = MEDUSA_BUFFER_CHUNKED_ENTRY_FLAG_DEFAULT;
                entry->offset = 0;
                entry->length = 0;
                entry->size = chunked->chunk_size;
                TAILQ_INSERT_TAIL(&chunked->entries, entry, list);
                chunked->total_size += entry->size;
        }
        return 0;
}

static int64_t chunked_buffer_get_size (const struct medusa_buffer *buffer)
{
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        return chunked->total_size;
}

static int64_t chunked_buffer_get_length (const struct medusa_buffer *buffer)
{
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        return chunked->total_length;
}

static int chunked_buffer_prepend (struct medusa_buffer *buffer, const void *data, int64_t length)
{
        int64_t w;
        int64_t l;
        unsigned int i;
        unsigned int c;
        struct medusa_buffer_chunked_entry *entry;
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        if (length < 0) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }
        if (MEDUSA_IS_ERR_OR_NULL(data)) {
                return -EINVAL;
        }
        c  = length;
        c += chunked->chunk_size - 1;
        c /= chunked->chunk_size;
        for (i = 0; i < c; i++) {
                entry = malloc(sizeof(struct medusa_buffer_chunked_entry) + chunked->chunk_size);
                if (entry == NULL) {
                        return -ENOMEM;
                }
                memset(entry, 0, sizeof(struct medusa_buffer_chunked_entry));
                entry->flags = MEDUSA_BUFFER_CHUNKED_ENTRY_FLAG_DEFAULT;
                entry->offset = 0;
                entry->length = 0;
                entry->size = chunked->chunk_size;
                TAILQ_INSERT_HEAD(&chunked->entries, entry, list);
                chunked->total_size += entry->size;
        }
        w = 0;
        TAILQ_FOREACH(entry, &chunked->entries, list) {
                if (w == length) {
                        break;
                }
                l = MIN(length - w, entry->size);
                memcpy(entry->data, data + w, l);
                w += l;
                entry->length = l;
                chunked->total_length += l;
        }
        return length;
}

static int chunked_buffer_append (struct medusa_buffer *buffer, const void *data, int64_t length)
{
        int rc;
        int64_t w;
        int64_t l;
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        if (length < 0) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }
        if (MEDUSA_IS_ERR_OR_NULL(data)) {
                return -EINVAL;
        }
        rc = chunked_buffer_resize(buffer, chunked->total_length + length);
        if (rc < 0) {
                return rc;
        }
        w = 0;
        while (w != length) {
                if (chunked->active == NULL) {
                        chunked->active = TAILQ_FIRST(&chunked->entries);
                        continue;
                }
                if (chunked->active->size - chunked->active->length <= 0) {
                        chunked->active = TAILQ_NEXT(chunked->active, list);
                        continue;
                }
                l = MIN(length - w, chunked->active->size - chunked->active->length);
                memcpy(chunked->active->data + chunked->active->length, data + w, l);
                w += l;
                chunked->active->length += l;
                chunked->total_length += l;
        }
        return length;
}

static int chunked_buffer_vprintf (struct medusa_buffer *buffer, const char *format, va_list va)
{
        int rc;
        int size;
        va_list vs;
        struct medusa_buffer_chunked_entry *entry;
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        va_copy(vs, va);
        size = vsnprintf(NULL, 0, format, vs);
        if (size < 0) {
                va_end(vs);
                return -EIO;
        }
        rc = chunked_buffer_resize(buffer, chunked->total_length + size + 1);
        if (rc < 0) {
                va_end(vs);
                return rc;
        }
        va_end(vs);
        if (chunked->active == NULL) {
                chunked->active = TAILQ_FIRST(&chunked->entries);
        }
        if (chunked->active->size - chunked->active->length < size + 1) {
                entry = malloc(sizeof(struct medusa_buffer_chunked_entry) + size + 1);
                if (entry == NULL) {
                        return -ENOMEM;
                }
                memset(entry, 0, sizeof(struct medusa_buffer_chunked_entry));
                entry->flags = MEDUSA_BUFFER_CHUNKED_ENTRY_FLAG_DEFAULT | MEDUSA_BUFFER_CHUNKED_ENTRY_FLAG_ALLOC;
                entry->offset = 0;
                entry->length = 0;
                entry->size = size + 1;
                TAILQ_INSERT_HEAD(&chunked->entries, entry, list);
                chunked->total_size += entry->size;
                chunked->active = entry;
        }
        va_copy(vs, va);
        rc = vsnprintf((char *) chunked->active->data + chunked->active->length, size + 1, format, vs);
        if (rc <= 0) {
                va_end(vs);
                return -EIO;
        }
        chunked->active->length += rc;
        chunked->total_length += rc;
        va_end(vs);
        return rc;
}

static int chunked_buffer_reserve (struct medusa_buffer *buffer, int64_t length, struct medusa_buffer_iovec *iovecs, int niovecs)
{
        int rc;
        int n;
        int64_t w;
        int64_t l;
        unsigned int c;
        struct medusa_buffer_chunked_entry *entry;
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        if (length < 0) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }
        if (niovecs < 0) {
                return -EINVAL;
        }
        if (niovecs == 0) {
                c  = length;
                c += chunked->chunk_size - 1;
                c /= chunked->chunk_size;
                return c;
        }
        rc = chunked_buffer_resize(buffer, chunked->total_length + length);
        if (rc < 0) {
                return rc;
        }
        if (chunked->active == NULL) {
                chunked->active = TAILQ_FIRST(&chunked->entries);
        }
        n = 0;
        w = 0;
        entry = chunked->active;
        while (n < niovecs && w != length) {
                if (entry == NULL) {
                        return -EIO;
                }
                if (entry->size - entry->length <= 0) {
                        entry = TAILQ_NEXT(entry, list);
                        continue;
                }
                l = MIN(length - w, entry->size - entry->length);
                w += l;
                iovecs[n].data   = entry->data + entry->length;
                iovecs[n].length = l;
                n += 1;
        }
        return n;
}

static int chunked_buffer_commit (struct medusa_buffer *buffer, const struct medusa_buffer_iovec *iovecs, int niovecs)
{
        int i;
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(iovecs)) {
                return -EINVAL;
        }
        if (niovecs <= 0) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(chunked->active)) {
                return -EIO;
        }
        for (i = 0; i < niovecs; i++) {
                if ((chunked->active->data > (uint8_t *) iovecs[i].data) ||
                    (chunked->active->data + chunked->active->length > (uint8_t *) iovecs[i].data) ||
                    (chunked->active->data + chunked->active->size < (uint8_t *) iovecs[i].data + iovecs[i].length)) {
                        return -EINVAL;
                }
                chunked->active->length += iovecs[i].length;
                chunked->active = TAILQ_NEXT(chunked->active, list);
        }
        return i;
}

static int chunked_buffer_peek (struct medusa_buffer *buffer, int64_t offset, int64_t length, struct medusa_buffer_iovec *iovecs, int niovecs)
{
        int rc;
        int n;
        int64_t w;
        int64_t l;
        unsigned int c;
        struct medusa_buffer_chunked_entry *entry;
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (niovecs < 0) {
                return -EINVAL;
        }
        if (length < 0) {
                length = chunked->total_length;
        } else {
                length = MIN(length, chunked->total_length);
        }
        if (length == 0) {
                return 0;
        }
        if (niovecs == 0) {
                c  = length;
                c += chunked->chunk_size - 1;
                c /= chunked->chunk_size;
                return c;
        }
        if (MEDUSA_IS_ERR_OR_NULL(chunked->active)) {
                return -EIO;
        }
        n = 0;
        w = 0;
        entry = chunked->active;
        while (n < niovecs && w != length) {
                if (entry == NULL) {
                        return -EIO;
                }
                if (entry->size - entry->length <= 0) {
                        entry = TAILQ_NEXT(entry, list);
                        continue;
                }
                l = MIN(length - w, entry->size - entry->length);
                w += l;
                iovecs[n].data   = entry->data + entry->length;
                iovecs[n].length = l;
                n += 1;
        }
        return n;
}

static int chunked_buffer_choke (struct medusa_buffer *buffer, int64_t length)
{
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        if (length < 0) {
                length = chunked->length;
        }
        if (chunked->length < length) {
                length = chunked->length;
        }
        if (chunked->length > length) {
                memmove(chunked->data, chunked->data + length, chunked->length - length);
                chunked->length -= length;
        } else {
                chunked->length = 0;
        }
        return 0;
}

static int chunked_buffer_reset (struct medusa_buffer *buffer)
{
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return -EINVAL;
        }
        chunked->length = 0;
        return 0;
}

static void chunked_buffer_destroy (struct medusa_buffer *buffer)
{
        struct medusa_buffer_chunked *chunked = (struct medusa_buffer_chunked *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return;;
        }
        if (chunked->data != NULL) {
                free(chunked->data);
        }
#if defined(MEDUSA_BUFFER_USE_POOL) && (MEDUSA_BUFFER_USE_POOL == 1)
        medusa_pool_free(chunked);
#else
        free(chunked);
#endif
}

const struct medusa_buffer_backend chunked_buffer_backend = {
        .resize         = chunked_buffer_resize,

        .get_size       = chunked_buffer_get_size,
        .get_length     = chunked_buffer_get_length,

        .prepend        = chunked_buffer_prepend,
        .append         = chunked_buffer_append,
        .vprintf        = chunked_buffer_vprintf,

        .reserve        = chunked_buffer_reserve,
        .commit         = chunked_buffer_commit,

        .peek           = chunked_buffer_peek,

        .choke          = chunked_buffer_choke,

        .reset          = chunked_buffer_reset,

        .destroy        = chunked_buffer_destroy
};

int medusa_buffer_chunked_init_options_default (struct medusa_buffer_chunked_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_buffer_chunked_init_options));
        options->flags = MEDUSA_BUFFER_CHUNKED_FLAG_DEFAULT;
        options->grow = MEDUSA_BUFFER_CHUNKED_DEFAULT_GROW;
        return 0;
}

struct medusa_buffer * medusa_buffer_chunked_create (unsigned int flags, unsigned int grow)
{
        int rc;
        struct medusa_buffer_chunked_init_options options;
        rc = medusa_buffer_chunked_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.flags = flags;
        options.grow = grow;
        return medusa_buffer_chunked_create_with_options(&options);

}

struct medusa_buffer * medusa_buffer_chunked_create_with_options (const struct medusa_buffer_chunked_init_options *options)
{
        struct medusa_buffer_chunked *chunked;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_BUFFER_USE_POOL) && (MEDUSA_BUFFER_USE_POOL == 1)
        chunked = medusa_pool_malloc(g_pool_buffer_chunked);
#else
        chunked = malloc(sizeof(struct medusa_buffer_chunked));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(chunked)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(chunked, 0, sizeof(struct medusa_buffer_chunked));
        chunked->grow = options->grow;
        if (chunked->grow <= 0) {
                chunked->grow = MEDUSA_BUFFER_CHUNKED_DEFAULT_GROW;
        }
        chunked->buffer.backend = &chunked_buffer_backend;
        return &chunked->buffer;
}

__attribute__ ((constructor)) static void buffer_chunked_constructor (void)
{
#if defined(MEDUSA_BUFFER_USE_POOL) && (MEDUSA_BUFFER_USE_POOL == 1)
        g_pool_buffer_chunked = medusa_pool_create("medusa-buffer-chunked", sizeof(struct medusa_buffer_chunked), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void buffer_chunked_destructor (void)
{
#if defined(MEDUSA_BUFFER_USE_POOL) && (MEDUSA_BUFFER_USE_POOL == 1)
        if (g_pool_buffer_chunked != NULL) {
                medusa_pool_destroy(g_pool_buffer_chunked);
        }
#endif
}
