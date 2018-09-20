
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include <sys/uio.h>

#include "error.h"
#include "buffer.h"
#include "buffer-struct.h"
#include "buffer-simple.h"
#include "buffer-chunked.h"

__attribute__ ((visibility ("default"))) int medusa_buffer_reset (struct medusa_buffer *buffer)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->reset)) {
                return -EINVAL;
        }
        return buffer->backend->reset(buffer);
}

__attribute__ ((visibility ("default"))) int64_t medusa_buffer_get_size (const struct medusa_buffer *buffer)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->get_size)) {
                return -EINVAL;
        }
        return buffer->backend->get_size(buffer);
}

__attribute__ ((visibility ("default"))) int64_t medusa_buffer_get_length (const struct medusa_buffer *buffer)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->get_length)) {
                return -EINVAL;
        }
        return buffer->backend->get_length(buffer);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_prepend (struct medusa_buffer *buffer, const void *data, int64_t length)
{
        int niovecs;
        struct iovec iovecs[1];
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(data)) {
                return -EINVAL;
        }
        if (length < 0) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }
        niovecs = 1;
        iovecs[0].iov_base = (void *) data;
        iovecs[0].iov_len  = length;
        return medusa_buffer_prependv(buffer, iovecs, niovecs);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_prependv (struct medusa_buffer *buffer, const struct iovec *iovecs, int niovecs)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->prependv)) {
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
        return buffer->backend->prependv(buffer, iovecs, niovecs);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_append (struct medusa_buffer *buffer, const void *data, int64_t length)
{
        int niovecs;
        struct iovec iovecs[1];
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(data)) {
                return -EINVAL;
        }
        if (length < 0) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }
        niovecs = 1;
        iovecs[0].iov_base = (void *) data;
        iovecs[0].iov_len  = length;
        return medusa_buffer_appendv(buffer, iovecs, niovecs);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_appendv (struct medusa_buffer *buffer, const struct iovec *iovecs, int niovecs)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->appendv)) {
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
        return buffer->backend->appendv(buffer, iovecs, niovecs);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_printf (struct medusa_buffer *buffer, const char *format, ...)
{
        int rc;
        va_list va;
        va_start(va, format);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                va_end(va);
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(format)) {
                va_end(va);
                return -EINVAL;
        }
        rc = medusa_buffer_vprintf(buffer, format, va);
        va_end(va);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_buffer_vprintf (struct medusa_buffer *buffer, const char *format, va_list va)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->vprintf)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(format)) {
                return -EINVAL;
        }
        return buffer->backend->vprintf(buffer, format, va);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_reserve (struct medusa_buffer *buffer, int64_t length, struct iovec *iovecs, int niovecs)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->reserve)) {
                return -EINVAL;
        }
        if (length < 0) {
                return -EINVAL;
        }
        if (niovecs < 0) {
                return -EINVAL;
        }
        return buffer->backend->reserve(buffer, length, iovecs, niovecs);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_commit (struct medusa_buffer *buffer, const struct iovec *iovecs, int niovecs)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->commit)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(iovecs)) {
                return -EINVAL;
        }
        if (niovecs < 0) {
                return -EINVAL;
        }
        return buffer->backend->commit(buffer, iovecs, niovecs);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_peek (struct medusa_buffer *buffer, int64_t offset, int64_t length, struct iovec *iovecs, int niovecs)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->peek)) {
                return -EINVAL;
        }
        return buffer->backend->peek(buffer, offset, length, iovecs, niovecs);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_choke (struct medusa_buffer *buffer, int64_t length)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->choke)) {
                return -EINVAL;
        }
        return buffer->backend->choke(buffer, length);
}

__attribute__ ((visibility ("default"))) int medusa_buffer_init_options_default (struct medusa_buffer_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_buffer_init_options));
        options->type = MEDUSA_BUFFER_TYPE_CHUNKED;
        options->flags = MEDUSA_BUFFER_FLAG_DEFAULT;
        options->u.chunked.chunk_size = MEDUSA_BUFFER_DEFAULT_CHUNK_SIZE;
        options->u.chunked.chunk_count = MEDUSA_BUFFER_DEFAULT_CHUNK_COUNT;
        return 0;
}

__attribute__ ((visibility ("default"))) struct medusa_buffer * medusa_buffer_create (unsigned int type)
{
        int rc;
        struct medusa_buffer_init_options options;
        rc = medusa_buffer_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.type = type;
        return medusa_buffer_create_with_options(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_buffer * medusa_buffer_create_with_options (const struct medusa_buffer_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (options->type == MEDUSA_BUFFER_TYPE_SIMPLE) {
                int rc;
                struct medusa_buffer_simple_init_options simple_options;
                rc = medusa_buffer_simple_init_options_default(&simple_options);
                if (rc < 0) {
                        return MEDUSA_ERR_PTR(rc);
                }
                simple_options.flags = MEDUSA_BUFFER_SIMPLE_FLAG_DEFAULT;
                simple_options.grow = options->u.simple.grow_size;
                return medusa_buffer_simple_create_with_options(&simple_options);
        } else if (options->type == MEDUSA_BUFFER_TYPE_CHUNKED) {
                int rc;
                struct medusa_buffer_chunked_init_options chunked_options;
                rc = medusa_buffer_chunked_init_options_default(&chunked_options);
                if (rc < 0) {
                        return MEDUSA_ERR_PTR(rc);
                }
                chunked_options.flags = MEDUSA_BUFFER_CHUNKED_FLAG_DEFAULT;
                chunked_options.chunk_size = options->u.chunked.chunk_size;
                chunked_options.chunk_count = options->u.chunked.chunk_count;
                return medusa_buffer_chunked_create_with_options(&chunked_options);
        } else {
                return MEDUSA_ERR_PTR(-ENOENT);
        }
}

__attribute__ ((visibility ("default"))) void medusa_buffer_destroy (struct medusa_buffer *buffer)
{
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                return;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend)) {
                return;
        }
        if (MEDUSA_IS_ERR_OR_NULL(buffer->backend->destroy)) {
                return;
        }
        buffer->backend->destroy(buffer);
}
