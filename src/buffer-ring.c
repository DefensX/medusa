
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
#include "buffer-ring.h"
#include "buffer-ring-struct.h"

#define MIN(a, b)                       (((a) < (b)) ? (a) : (b))

#define MEDUSA_BUFFER_RING_USE_POOL   1
#if defined(MEDUSA_BUFFER_RING_USE_POOL) && (MEDUSA_BUFFER_RING_USE_POOL == 1)
static struct medusa_pool *g_pool_buffer_ring;
#endif

static int ring_buffer_headify (struct medusa_buffer_ring *ring)
{
        void *data;
        if (ring->length == 0) {
                ring->head = 0;
                return 0;
        }
        if (ring->head == 0) {
                return 0;
        }
        if (ring->head + ring->length <= ring->size) {
                memmove(ring->data, ring->data + ring->head, ring->length);
                ring->head = 0;
                return 0;
        }
        data = malloc(ring->size);
        if (data == NULL) {
                return -ENOMEM;
        }
        memcpy(data, ring->data + ring->head, ring->size - ring->head);
        memcpy(data + ring->size - ring->head, ring->data, ring->length - (ring->size - ring->head));
        free(ring->data);
        ring->data = data;
        ring->head = 0;
        return 0;
}

static int ring_buffer_resize (struct medusa_buffer_ring *ring, int64_t nsize)
{
        int rc;
        void *data;
        unsigned int size;
        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }
        if (nsize < 0) {
                return -EINVAL;
        }
        if (nsize < ring->length) {
                return -EINVAL;
        }
        if (nsize == 0) {
                free(ring->data);
                ring->data = NULL;
                ring->size = 0;
                ring->head = 0;
                return 0;
        }
        size  = nsize / ring->grow;
        size += (nsize % ring->grow) ? 1 : 0;
        size *= ring->grow;
        rc = ring_buffer_headify(ring);
        if (rc != 0) {
                return -EIO;
        }
#if 1
        data = realloc(ring->data, size);
        if (data == NULL) {
#else
        if (1) {
#endif
                data = malloc(size);
                if (data == NULL) {
                        return -ENOMEM;
                }
                if (ring->length > 0) {
                        memcpy(data, ring->data, ring->length);
                }
                free(ring->data);
                ring->data = data;
        } else {
                ring->data = data;
        }
        ring->size = size;
        return 0;
}

static int64_t ring_buffer_get_size (const struct medusa_buffer *buffer)
{
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }
        return ring->size;
}

static int64_t ring_buffer_get_length (const struct medusa_buffer *buffer)
{
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }
        return ring->length;
}

static int64_t ring_buffer_insertv (struct medusa_buffer *buffer, int64_t offset, const struct medusa_iovec *iovecs, int64_t niovecs)
{
        int rc;

        int64_t i;
        int64_t length;

        int64_t len;
        int64_t src;
        int64_t dst;

        int64_t srcbeg;
        int64_t srcend;
        int64_t dstbeg;
        int64_t dstend;

        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;

        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }

        if (offset < 0) {
                offset = ring->length + offset;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (offset > ring->length) {
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
        if (length == 0) {
                return 0;
        }

        rc = ring_buffer_resize(ring, ring->length + length);
        if (rc < 0) {
                return rc;
        }

        if (offset == 0 &&
            ring->length != 0) {
                ring->head = ring->head - length;
                if (ring->head < 0) {
                        ring->head += ring->size;
                }
        } else if (offset != ring->length) {
                srcbeg = ring->head + offset;
                srcend = ring->head + ring->length;
                dstbeg = srcbeg + length;
                dstend = srcend + length;

                /*
                 *  H      SrcBeg      DstBeg      SrcEnd      DstEnd
                 *  |         |           |           |           |
                 *  |         *************************           |
                 *  |                     |                       |
                 *  |                     +++++++++++++++++++++++++
                 *  ------------------------------------------------------
                 *      S1         S2          S3          S4           S5
                 */
                if (srcbeg >= ring->size) {
                        len = srcend - srcbeg;
                        src = srcbeg - ring->size;
                        dst = dstbeg - ring->size;
                        memmove(ring->data + dst, ring->data + src, len);
                } else if (dstbeg >= ring->size) {
                        len = srcend - ring->size;
                        src = 0;
                        dst = length;
                        memmove(ring->data + dst, ring->data + src, len);

                        len = ring->size - srcbeg;
                        src = srcbeg;
                        dst = dstbeg - ring->size;
                        memmove(ring->data + dst, ring->data + src, len);
                } else if (srcend > ring->size) {
                        len = srcend - ring->size;
                        src = 0;
                        dst = src + length;
                        memmove(ring->data + dst, ring->data + src, len);

                        len = length;
                        src = ring->size - len;
                        dst = 0;
                        memmove(ring->data + dst, ring->data + src, len);

                        len = ring->size - dstbeg;
                        src = srcbeg;
                        dst = dstbeg;
                        memmove(ring->data + dst, ring->data + src, len);
                } else if (dstend > ring->size) {
                        len = dstend - ring->size;
                        src = srcend - len;
                        dst = 0;
                        memmove(ring->data + dst, ring->data + src, len);

                        len = ring->size - (dstend - ring->size);
                        src = srcbeg;
                        dst = dstbeg;
                        memmove(ring->data + dst, ring->data + src, len);
                } else {
                        len = srcend - srcbeg;
                        src = srcbeg;
                        dst = dstbeg;
                        memmove(ring->data + dst, ring->data + src, len);
                }
        }

        length = 0;
        for (i = 0; i < niovecs; i++) {
                dstbeg = ring->head + offset + length;
                dstend = dstbeg + iovecs[i].iov_len;
                /*
                 *  H      DstBeg    DstEnd
                 *  |         |         |
                 *  |         ***********
                 *  ----------------------------
                 *      S1         S2          S3
                 */
                if (dstbeg >= ring->size) {
                        len = dstend - dstbeg;
                        src = 0;
                        dst = dstbeg - ring->size;
                        memmove(ring->data + dst, iovecs[i].iov_base + src, len);
                } else if (dstend > ring->size) {
                        len = ring->size - dstbeg;
                        src = 0;
                        dst = dstbeg;
                        memmove(ring->data + dst, iovecs[i].iov_base + src, len);

                        len = dstend - ring->size;
                        src = ring->size - dstbeg;
                        dst = 0;
                        memmove(ring->data + dst, iovecs[i].iov_base + src, len);
                } else {
                        len = dstend - dstbeg;
                        src = 0;
                        dst = dstbeg;
                        memmove(ring->data + dst, iovecs[i].iov_base + src, len);
                }
                length += iovecs[i].iov_len;
        }

        ring->length += length;
        return length;
}

static int64_t ring_buffer_insertfv (struct medusa_buffer *buffer, int64_t offset, const char *format, va_list va)
{
        int rc;
        va_list vs;

        int length;

        int64_t len;
        int64_t src;
        int64_t dst;

        int64_t srcbeg;
        int64_t srcend;
        int64_t dstbeg;
        int64_t dstend;

        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;

        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }

        if (offset < 0) {
                offset = ring->length + offset;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (offset > ring->length) {
                return -EINVAL;
        }

        va_copy(vs, va);
        length = vsnprintf(NULL, 0, format, vs);
        va_end(vs);
        if (length < 0) {
                return -EIO;
        }
        length += 1;

        rc = ring_buffer_resize(ring, ring->length + length);
        if (rc < 0) {
                return rc;
        }

again:
        if (offset == 0 &&
            ring->head >= length) {
                ring->head = ring->head - length;
                if (ring->head < 0) {
                        ring->head += ring->size;
                }
        } else if (offset != ring->length) {
                srcbeg = ring->head + offset;
                srcend = ring->head + ring->length;
                dstbeg = srcbeg + length;
                dstend = srcend + length;

                /*
                 *  H      SrcBeg      DstBeg      SrcEnd      DstEnd
                 *  |         |           |           |           |
                 *  |         *************************           |
                 *  |                     |                       |
                 *  |                     +++++++++++++++++++++++++
                 *  ------------------------------------------------------
                 *      S1         S2          S3          S4           S5
                 */
                if (srcbeg >= ring->size) {
                        len = srcend - srcbeg;
                        src = srcbeg - ring->size;
                        dst = dstbeg - ring->size;
                        memmove(ring->data + dst, ring->data + src, len);
                } else if (dstbeg > ring->size) {
                        rc = ring_buffer_headify(ring);
                        if (rc < 0) {
                                return -EIO;
                        }
                        goto again;
                } else if (srcend > ring->size) {
                        len = srcend - ring->size;
                        src = 0;
                        dst = src + length;
                        memmove(ring->data + dst, ring->data + src, len);

                        len = length;
                        src = ring->size - len;
                        dst = 0;
                        memmove(ring->data + dst, ring->data + src, len);

                        len = ring->size - dstbeg;
                        src = srcbeg;
                        dst = dstbeg;
                        memmove(ring->data + dst, ring->data + src, len);
                } else if (dstend > ring->size) {
                        len = dstend - ring->size;
                        src = srcend - len;
                        dst = 0;
                        memmove(ring->data + dst, ring->data + src, len);

                        len = ring->size - (dstend - ring->size);
                        src = srcbeg;
                        dst = dstbeg;
                        memmove(ring->data + dst, ring->data + src, len);
                } else {
                        len = srcend - srcbeg;
                        src = srcbeg;
                        dst = dstbeg;
                        memmove(ring->data + dst, ring->data + src, len);
                }
        } else if (offset == ring->length &&
                   ring->head + offset + length > ring->size) {
                rc = ring_buffer_headify(ring);
                if (rc < 0) {
                        return -EIO;
                }
                goto again;
        }

        len = length;
        dst = (ring->head + offset) % ring->size;

        va_copy(vs, va);
        rc = vsnprintf(ring->data + dst, len, format, vs);
        va_end(vs);
        if (rc < 0) {
                return -EIO;
        }
        ring->length += rc;
        return rc;
}

static int64_t ring_buffer_reservev (struct medusa_buffer *buffer, int64_t length, struct medusa_iovec *iovecs, int64_t niovecs)
{
        int rc;
        int64_t dstbeg;
        int64_t dstend;
        int64_t riovecs;
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;

        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
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

        if (ring->size - ring->length < length) {
                riovecs = 1;
        } else if (ring->head + ring->length < ring->size &&
                   ring->head + ring->length + length > ring->size) {
                riovecs = 2;
        } else {
                riovecs = 1;
        }

        if (niovecs == 0) {
                return riovecs;
        }

        rc = ring_buffer_resize(ring, ring->length + length);
        if (rc < 0) {
                return rc;
        }

        dstbeg = ring->head + ring->length;
        dstend = ring->head + ring->length + length;

        /*
         *  H      DstBeg    DstEnd
         *  |         |         |
         *  |         ***********
         *  ----------------------------
         *      S1         S2          S3
         */

        if (dstbeg >= ring->size) {
                iovecs[0].iov_base = ring->data + dstbeg - ring->size;
                iovecs[0].iov_len  = length;
                riovecs = 1;
        } else if (dstend > ring->size) {
                iovecs[0].iov_base = ring->data + dstbeg;
                iovecs[0].iov_len  = ring->size - dstbeg;;
                riovecs = 1;
                if (niovecs > 1) {
                        iovecs[1].iov_base = ring->data + 0;
                        iovecs[1].iov_len  = dstend - ring->size;
                        riovecs = 2;
                }
        } else {
                iovecs[0].iov_base = ring->data + dstbeg;
                iovecs[0].iov_len  = length;
                riovecs = 1;
        }
        return riovecs;
}

static int64_t ring_buffer_commitv (struct medusa_buffer *buffer, const struct medusa_iovec *iovecs, int64_t niovecs)
{
        int64_t i;
        int64_t l;
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;

        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
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

        for (i = 0, l = 0; i < niovecs; i++) {
                if (ring->data + (ring->head + ring->length + l) % ring->size != iovecs[i].iov_base) {
                        return -EINVAL;
                }
                if (ring->length + l > ring->size) {
                        return -EINVAL;
                }
                l += iovecs[i].iov_len;
        }

        ring->length += l;
        return niovecs;
}

static int64_t ring_buffer_peekv (const struct medusa_buffer *buffer, int64_t offset, int64_t length, struct medusa_iovec *iovecs, int64_t niovecs)
{
        int64_t srcbeg;
        int64_t srcend;
        int64_t riovecs;
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;

        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }

        if (niovecs < 0) {
                return -EINVAL;
        }
        if (offset < 0) {
                offset = ring->length + offset;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (offset > ring->length) {
                return -EINVAL;
        }
        if (length < 0) {
                length = ring->length - offset;
        }
        if (offset + length > ring->length) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }

        srcbeg = ring->head + offset;
        srcend = ring->head + offset + length;
        /*
         *  H      SrcBeg    SrcEnd
         *  |         |         |
         *  |         ***********
         *  ----------------------------
         *      S1         S2          S3
         */
        if (srcbeg > ring->size) {
                riovecs = 1;
        } else if (srcend > ring->size) {
                riovecs = 2;
        } else {
                riovecs = 1;
        }
        if (niovecs == 0) {
                return riovecs;
        }

        if (srcbeg >= ring->size) {
                iovecs[0].iov_base = ring->data + srcbeg - ring->size;
                iovecs[0].iov_len  = srcend - srcbeg;
                riovecs = 1;
        } else if (srcend > ring->size) {
                iovecs[0].iov_base = ring->data + srcbeg;
                iovecs[0].iov_len  = ring->size - srcbeg;
                riovecs = 1;
                if (niovecs > 1) {
                        iovecs[1].iov_base = ring->data + 0;
                        iovecs[1].iov_len  = srcend - ring->size;
                        riovecs = 2;
                }
        } else {
                iovecs[0].iov_base = ring->data + srcbeg;
                iovecs[0].iov_len  = srcend - srcbeg;
                riovecs = 1;
        }
        return riovecs;
}

static int64_t ring_buffer_choke (struct medusa_buffer *buffer, int64_t offset, int64_t length)
{
        int64_t len;
        int64_t src;
        int64_t dst;

        int64_t srcbeg;
        int64_t srcend;
        int64_t dstbeg;
        int64_t dstend;

        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;

        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }

        if (offset < 0) {
                offset = ring->length + offset;
        }
        if (offset < 0) {
                return -EINVAL;
        }
        if (offset > ring->length) {
                return -EINVAL;
        }
        if (length < 0) {
                length = ring->length - offset;
        }
        if (offset + length > ring->length) {
                return -EINVAL;
        }
        if (length == 0) {
                return 0;
        }

        if (offset == 0) {
                if (length == ring->length) {
                        ring->head   = 0;
                        ring->length = 0;
                } else {
                        ring->head   += length;
                        ring->head   %= ring->size;
                        ring->length -= length;
                }
                return length;
        }
        if (ring->head + offset + length == ring->length) {
                ring->length -= length;
                return length;
        }

        srcbeg = ring->head + offset + length;
        srcend = ring->head + ring->length;
        dstbeg = srcbeg - length;
        dstend = srcend - length;

        /*
         *  H      DstBeg      SrcBeg      DstEnd      SrcEnd
         *  |         |           |           |           |
         *  |         *************************           |
         *  |                     |                       |
         *  |                     +++++++++++++++++++++++++
         *  ------------------------------------------------------
         *      S1         S2          S3          S4           S5
         */
        if (dstbeg >= ring->size) {
                len = srcend - srcbeg;
                src = srcbeg - ring->size;
                dst = dstbeg - ring->size;
                memmove(ring->data + dst, ring->data + src, len);
        } else if (srcbeg >= ring->size) {
                len = ring->size - dstbeg;
                src = srcbeg - ring->size;
                dst = dstbeg;
                memmove(ring->data + dst, ring->data + src, len);

                len = dstend - ring->size;
                src = srcend - len;
                dst = 0;
                memmove(ring->data + dst, ring->data + src, len);
        } else if (dstend > ring->size) {
                len = ring->size - srcbeg;
                src = srcbeg;
                dst = dstbeg;
                memmove(ring->data + dst, ring->data + src, len);

                len = length;
                src = 0;
                dst = ring->size - length;
                memmove(ring->data + dst, ring->data + src, len);

                len = srcend - ring->size - length;
                src = length;
                dst = 0;
                memmove(ring->data + dst, ring->data + src, len);
        } else if (dstend > ring->size) {
                len = ring->size - srcbeg;
                src = srcbeg;
                dst = dstbeg;
                memmove(ring->data + dst, ring->data + src, len);

                len = srcend - ring->size;
                src = 0;
                dst = dstbeg + ring->size - srcbeg;
                memmove(ring->data + dst, ring->data + src, len);
        } else {
                len = srcend - srcbeg;
                src = srcbeg;
                dst = dstbeg;
                memmove(ring->data + dst, ring->data + src, len);
        }

        ring->length -= length;
        return length;
}

static void * ring_buffer_linearize (struct medusa_buffer *buffer, int64_t offset, int64_t length)
{
        int rc;
        int64_t srcbeg;
        int64_t srcend;
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;

        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }

        if (offset < 0) {
                offset = ring->length + offset;
        }
        if (offset < 0) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (offset > ring->length) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (length < 0) {
                length = ring->length - offset;
        }
        if (offset + length > ring->length) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }

again:
        srcbeg = ring->head + offset;
        srcend = ring->head + offset + length;
        /*
         *  H      SrcBeg    SrcEnd      H + L
         *  |         |         |          |
         *  |         ***********          |
         *  --------------------------------
         *      S1         S2          S3
         */
        if (srcbeg >= ring->size) {
                return ring->data + ring->head + offset - ring->size;
        } else if (srcend > ring->size) {
                if (ring->length + length <= ring->size) {
                        struct medusa_iovec iovecs[1];
                        iovecs[0].iov_base = ring->data + srcbeg;
                        iovecs[0].iov_len  = ring->size - srcbeg;
                        rc = ring_buffer_insertv(buffer, ring->size - ring->head, iovecs, 1);
                        if (rc != (int) iovecs[0].iov_len) {
                                return MEDUSA_ERR_PTR(-EIO);
                        }
                        memmove(ring->data + ring->head + iovecs[0].iov_len, ring->data + ring->head, ring->size - ring->head - iovecs[0].iov_len);
                        ring->head   += iovecs[0].iov_len;
                        ring->length -= iovecs[0].iov_len;
                } else {
                        rc = ring_buffer_headify(ring);
                        if (rc < 0) {
                                return MEDUSA_ERR_PTR(-EIO);
                        }
                }
                goto again;
        } else {
                return ring->data + ring->head + offset;
        }
}

static int ring_buffer_shrink (struct medusa_buffer *buffer, int64_t size)
{
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }
        if (size < 0) {
                return -EINVAL;
        }
        if (size < ring->length) {
                return -EINVAL;
        }
        if (size >= ring->size) {
                return 0;
        }
        return ring_buffer_resize(ring, size);
}

static int ring_buffer_reset (struct medusa_buffer *buffer)
{
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return -EINVAL;
        }
        ring->length = 0;
        ring->head   = 0;
        return 0;
}

static void ring_buffer_destroy (struct medusa_buffer *buffer)
{
        struct medusa_buffer_ring *ring = (struct medusa_buffer_ring *) buffer;
        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return;;
        }
        if (ring->data != NULL) {
                free(ring->data);
        }
#if defined(MEDUSA_BUFFER_RING_USE_POOL) && (MEDUSA_BUFFER_RING_USE_POOL == 1)
        medusa_pool_free(ring);
#else
        free(ring);
#endif
}

const struct medusa_buffer_backend ring_buffer_backend = {
        .get_size       = ring_buffer_get_size,
        .get_length     = ring_buffer_get_length,

        .insertv        = ring_buffer_insertv,
        .insertfv       = ring_buffer_insertfv,

        .reservev       = ring_buffer_reservev,
        .commitv        = ring_buffer_commitv,

        .peekv          = ring_buffer_peekv,
        .choke          = ring_buffer_choke,

        .linearize      = ring_buffer_linearize,
        .shrink         = ring_buffer_shrink,

        .reset          = ring_buffer_reset,
        .destroy        = ring_buffer_destroy
};

int medusa_buffer_ring_init_options_default (struct medusa_buffer_ring_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_buffer_ring_init_options));
        options->flags = MEDUSA_BUFFER_RING_FLAG_DEFAULT;
        options->grow = MEDUSA_BUFFER_RING_DEFAULT_GROW;
        return 0;
}

struct medusa_buffer * medusa_buffer_ring_create (unsigned int flags, unsigned int grow)
{
        int rc;
        struct medusa_buffer_ring_init_options options;
        rc = medusa_buffer_ring_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.flags = flags;
        options.grow  = grow;
        return medusa_buffer_ring_create_with_options(&options);
}

struct medusa_buffer * medusa_buffer_ring_create_with_options (const struct medusa_buffer_ring_init_options *options)
{
        struct medusa_buffer_ring *ring;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_BUFFER_RING_USE_POOL) && (MEDUSA_BUFFER_RING_USE_POOL == 1)
        ring = medusa_pool_malloc(g_pool_buffer_ring);
#else
        ring = malloc(sizeof(struct medusa_buffer_ring));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(ring)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(ring, 0, sizeof(struct medusa_buffer_ring));
        ring->grow   = options->grow;
        ring->length = 0;
        ring->size   = 0;
        ring->head   = 0;
        ring->data   = NULL;
        if (ring->grow <= 0) {
                ring->grow = MEDUSA_BUFFER_RING_DEFAULT_GROW;
        }
        ring->buffer.backend = &ring_buffer_backend;
        return &ring->buffer;
}

__attribute__ ((constructor)) static void buffer_ring_constructor (void)
{
#if defined(MEDUSA_BUFFER_RING_USE_POOL) && (MEDUSA_BUFFER_RING_USE_POOL == 1)
        g_pool_buffer_ring = medusa_pool_create("medusa-buffer-ring", sizeof(struct medusa_buffer_ring), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void buffer_ring_destructor (void)
{
#if defined(MEDUSA_BUFFER_RING_USE_POOL) && (MEDUSA_BUFFER_RING_USE_POOL == 1)
        if (g_pool_buffer_ring != NULL) {
                medusa_pool_destroy(g_pool_buffer_ring);
        }
#endif
}
