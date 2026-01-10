
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include "error.h"
#include "pool.h"
#include "iovec.h"
#include "buffer.h"
#include "buffer-struct.h"
#include "buffer-simple.h"
#include "buffer-simple-struct.h"

#define MIN(a, b)                       (((a) < (b)) ? (a) : (b))

#define MEDUSA_BUFFER_SIMPLE_USE_POOL   1
#if defined(MEDUSA_BUFFER_SIMPLE_USE_POOL) && (MEDUSA_BUFFER_SIMPLE_USE_POOL == 1)
static struct medusa_pool *g_pool_buffer_simple;
#endif

static int simple_buffer_resize (struct medusa_buffer_simple *simple, int64_t nsize)
{
        void *data;
        unsigned int size;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        if (nsize < 0) {
                return -EINVAL;
        }
        if (nsize < simple->length) {
                return -EINVAL;
        }
        if (nsize == 0) {
                free(simple->data);
                simple->data = NULL;
                simple->size = 0;
                return 0;
        }
        size  = nsize / simple->grow;
        size += (nsize % simple->grow) ? 1 : 0;
        size *= simple->grow;
#if 1
        data = realloc(simple->data, size);
        if (data == NULL) {
#else
        if (1) {
#endif
                data = malloc(size);
                if (data == NULL) {
                        return -ENOMEM;
                }
                if (simple->length > 0) {
                        memcpy(data, simple->data, simple->length);
                }
                free(simple->data);
                simple->data = data;
        } else {
                simple->data = data;
        }
        simple->size = size;
        return 0;
}

static int64_t simple_buffer_get_size (const struct medusa_buffer *buffer)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        return simple->size;
}

static int64_t simple_buffer_get_length (const struct medusa_buffer *buffer)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        return simple->length;
}

static int64_t simple_buffer_insertv (struct medusa_buffer *buffer, int64_t offset, const struct medusa_iovec *iovecs, int64_t niovecs)
{
        int rc;
        int64_t i;
        int64_t length;
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        if (offset < 0) {
                offset = simple->length + offset;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (offset > simple->length) {
                return -EINVAL;
        }
        if (niovecs < 0) {
                return -EINVAL;
        }
        if (niovecs == 0) {
                return 0;
        }
        if (MEDUSA_IS_ERR_OR_NULL(iovecs)) {
                return -EINVAL;
        }
        length = 0;
        for (i = 0; i < niovecs; i++) {
                length += iovecs[i].iov_len;
        }
        if (simple->size < simple->length + length) {
                rc = simple_buffer_resize(simple, simple->length + length);
                if (rc < 0) {
                        return rc;
                }
        }
        if (offset != simple->length) {
                memmove(simple->data + offset + length, simple->data + offset, simple->length - offset);
        }
        length = 0;
        for (i = 0; i < niovecs; i++) {
                memmove(simple->data + offset + length, iovecs[i].iov_base, iovecs[i].iov_len);
                length += iovecs[i].iov_len;
        }
        simple->length += length;
        return length;
}

static int64_t simple_buffer_insertfv (struct medusa_buffer *buffer, int64_t offset, const char *format, va_list va)
{
        int rc;
        int length;
        va_list vs;
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        if (offset < 0) {
                offset = simple->length + offset;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (offset > simple->length) {
                return -EINVAL;
        }
        va_copy(vs, va);
        length = vsnprintf(NULL, 0, format, vs);
        va_end(vs);
        if (length < 0) {
                return -EIO;
        }
        length += 1;
        if (simple->size < simple->length + length) {
                rc = simple_buffer_resize(simple, simple->length + length);
                if (rc < 0) {
                        return rc;
                }
        }
        if (offset != simple->length) {
                memmove(simple->data + offset + length, simple->data + offset, simple->length - offset);
        }
        va_copy(vs, va);
        rc = vsnprintf(simple->data + offset, length, format, vs);
        va_end(vs);
        if (rc < 0) {
                return -EIO;
        }
        simple->length += rc;
        return rc;
}

static int64_t simple_buffer_reservev (struct medusa_buffer *buffer, int64_t length, struct medusa_iovec *iovecs, int64_t niovecs)
{
        int rc;
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
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
                return 1;
        }
        if (simple->size < simple->length + length) {
                rc = simple_buffer_resize(simple, simple->length + length);
                if (rc < 0) {
                        return rc;
                }
        }
        iovecs[0].iov_base = simple->data + simple->length;
        iovecs[0].iov_len  = length;
        return 1;
}

static int64_t simple_buffer_commitv (struct medusa_buffer *buffer, const struct medusa_iovec *iovecs, int64_t niovecs)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(iovecs)) {
                return -EINVAL;
        }
        if (niovecs < 0) {
                return -EINVAL;
        }
        if (niovecs == 0) {
                return 0;
        }
        if (niovecs != 1) {
                return -EINVAL;
        }
        if ((simple->data + simple->length != iovecs[0].iov_base) ||
            (simple->data + simple->size < iovecs[0].iov_base + iovecs[0].iov_len)) {
                return -EINVAL;
        }
        simple->length += iovecs[0].iov_len;
        return 1;
}

static int64_t simple_buffer_peekv (const struct medusa_buffer *buffer, int64_t offset, int64_t length, struct medusa_iovec *iovecs, int64_t niovecs)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        if (niovecs < 0) {
                return -EINVAL;
        }
        if (offset < 0) {
                offset = simple->length + offset;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (offset > simple->length) {
                return -EINVAL;
        }
        if (length < 0) {
                length = simple->length - offset;
        }
        if (offset + length > simple->length) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }
        if (niovecs == 0) {
                return 1;
        }
        iovecs[0].iov_base = simple->data + offset;
        iovecs[0].iov_len  = length;
        return 1;
}

static int64_t simple_buffer_choke (struct medusa_buffer *buffer, int64_t offset, int64_t length)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        if (offset < 0) {
                offset = simple->length + offset;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (offset > simple->length) {
                return -EINVAL;
        }
        if (length < 0) {
                length = simple->length - offset;
        }
        if (offset + length > simple->length) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }
        simple->length -= length;
        if (simple->length > 0) {
                memmove(simple->data + offset, simple->data + offset + length, simple->length - offset);
        }
        return length;
}

static void * simple_buffer_linearize (struct medusa_buffer *buffer, int64_t offset, int64_t length)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (offset < 0) {
                offset = simple->length + offset;
        }
        if (offset < 0) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (offset > simple->length) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (length < 0) {
                length = simple->length - offset;
        }
        if (offset + length > simple->length) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return simple->data + offset;
}

static int simple_buffer_shrink (struct medusa_buffer *buffer, int64_t size)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        if (size < 0) {
                return -EINVAL;
        }
        if (size < simple->length) {
                return -EINVAL;
        }
        if (size >= simple->size) {
                return 0;
        }
        return simple_buffer_resize(simple, size);
}

static int simple_buffer_reset (struct medusa_buffer *buffer)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return -EINVAL;
        }
        simple->length = 0;
        return 0;
}

static void simple_buffer_destroy (struct medusa_buffer *buffer)
{
        struct medusa_buffer_simple *simple = (struct medusa_buffer_simple *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return;;
        }
        if (simple->data != NULL) {
                free(simple->data);
        }
#if defined(MEDUSA_BUFFER_SIMPLE_USE_POOL) && (MEDUSA_BUFFER_SIMPLE_USE_POOL == 1)
        medusa_pool_free(simple);
#else
        free(simple);
#endif
}

const struct medusa_buffer_backend simple_buffer_backend = {
        .get_size       = simple_buffer_get_size,
        .get_length     = simple_buffer_get_length,

        .insertv        = simple_buffer_insertv,
        .insertfv       = simple_buffer_insertfv,

        .reservev       = simple_buffer_reservev,
        .commitv        = simple_buffer_commitv,

        .peekv          = simple_buffer_peekv,
        .choke          = simple_buffer_choke,

        .linearize      = simple_buffer_linearize,
        .shrink         = simple_buffer_shrink,

        .reset          = simple_buffer_reset,
        .destroy        = simple_buffer_destroy
};

int medusa_buffer_simple_init_options_default (struct medusa_buffer_simple_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_buffer_simple_init_options));
        options->flags = MEDUSA_BUFFER_SIMPLE_FLAG_DEFAULT;
        options->grow = MEDUSA_BUFFER_SIMPLE_DEFAULT_GROW;
        return 0;
}

struct medusa_buffer * medusa_buffer_simple_create (unsigned int flags, unsigned int grow)
{
        int rc;
        struct medusa_buffer_simple_init_options options;
        rc = medusa_buffer_simple_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.flags = flags;
        options.grow  = grow;
        return medusa_buffer_simple_create_with_options(&options);
}

struct medusa_buffer * medusa_buffer_simple_create_with_options (const struct medusa_buffer_simple_init_options *options)
{
        struct medusa_buffer_simple *simple;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_BUFFER_SIMPLE_USE_POOL) && (MEDUSA_BUFFER_SIMPLE_USE_POOL == 1)
        simple = medusa_pool_malloc(g_pool_buffer_simple);
#else
        simple = malloc(sizeof(struct medusa_buffer_simple));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(simple)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(simple, 0, sizeof(struct medusa_buffer_simple));
        simple->grow = options->grow;
        if (simple->grow <= 0) {
                simple->grow = MEDUSA_BUFFER_SIMPLE_DEFAULT_GROW;
        }
        simple->buffer.backend = &simple_buffer_backend;
        return &simple->buffer;
}

__attribute__ ((constructor)) static void buffer_simple_constructor (void)
{
#if defined(MEDUSA_BUFFER_SIMPLE_USE_POOL) && (MEDUSA_BUFFER_SIMPLE_USE_POOL == 1)
        g_pool_buffer_simple = medusa_pool_create("medusa-buffer-simple", sizeof(struct medusa_buffer_simple), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void buffer_simple_destructor (void)
{
#if defined(MEDUSA_BUFFER_SIMPLE_USE_POOL) && (MEDUSA_BUFFER_SIMPLE_USE_POOL == 1)
        if (g_pool_buffer_simple != NULL) {
                medusa_pool_destroy(g_pool_buffer_simple);
        }
#endif
}
