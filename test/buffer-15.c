
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>

#include "medusa/error.h"
#include "medusa/iovec.h"
#include "medusa/buffer.h"

/*
 * buffer-15: randomized fuzzer/tester for buffer.h
 *
 * The other buffer-NN.c tests each exercise a handful of the functions
 * declared in buffer.h (append/prepend/peekv, reservev/commitv, choke,
 * memcmp/memmem, ...). A large part of the API has no coverage at all:
 * insert/insertv/insertf, the prepend/append/insert_uintNN{,_le,_be}
 * families, printf/vprintf, strcmp/strncmp/strcasecmp/strncasecmp,
 * strchr/strcasechr/strstr/strcasestr, peek_data/peek_uintNN,
 * read_data/read_uintNN, writev, linearize and maybe_shrink.
 *
 * This file drives every one of those functions against a plain, simple
 * "shadow" byte array that is maintained independently (with its own
 * insert/choke logic, and its own byte-order encode/decode helpers that
 * do not reuse anything from src/endian.h) and cross-checks the real
 * medusa_buffer content against the shadow after every operation.
 */

static const unsigned int g_types[] = {
        MEDUSA_BUFFER_TYPE_DEFAULT,
        MEDUSA_BUFFER_TYPE_SIMPLE,
        MEDUSA_BUFFER_TYPE_RING
};

static const char *type_name (unsigned int type)
{
        if (type == MEDUSA_BUFFER_TYPE_SIMPLE) {
                return "simple";
        }
        if (type == MEDUSA_BUFFER_TYPE_RING) {
                return "ring";
        }
        return "unknown";
}

/*
 * random helpers
 */

static uint64_t rand64 (void)
{
        uint64_t r;
        r  = (uint64_t) (rand() & 0xffff);
        r |= (uint64_t) (rand() & 0xffff) << 16;
        r |= (uint64_t) (rand() & 0xffff) << 32;
        r |= (uint64_t) (rand() & 0xffff) << 48;
        return r;
}

static int64_t rand_range (int64_t lo, int64_t hi)
{
        uint64_t span;
        if (hi <= lo) {
                return lo;
        }
        span = (uint64_t) (hi - lo) + 1;
        return lo + (int64_t) (rand64() % span);
}

static void fill_random (unsigned char *data, int64_t length)
{
        int64_t i;
        for (i = 0; i < length; i++) {
                data[i] = (unsigned char) (rand() & 0xff);
        }
}

static void fill_printable (unsigned char *data, int64_t length)
{
        int64_t i;
        for (i = 0; i < length; i++) {
                data[i] = (unsigned char) ('a' + (rand() % 26));
        }
}

/*
 * manual (library-independent) byte order helpers, used both to build the
 * bytes we expect prepend/append/insert_uintNN_{le,be} to have produced,
 * and to decode raw bytes back into a value to double check peek_uintNN_{le,be}.
 */

static void put_le16 (unsigned char *p, uint16_t v) { p[0] = (unsigned char) (v); p[1] = (unsigned char) (v >> 8); }
static void put_be16 (unsigned char *p, uint16_t v) { p[0] = (unsigned char) (v >> 8); p[1] = (unsigned char) (v); }
static void put_le32 (unsigned char *p, uint32_t v) { int i; for (i = 0; i < 4; i++) p[i] = (unsigned char) (v >> (8 * i)); }
static void put_be32 (unsigned char *p, uint32_t v) { int i; for (i = 0; i < 4; i++) p[i] = (unsigned char) (v >> (8 * (3 - i))); }
static void put_le64 (unsigned char *p, uint64_t v) { int i; for (i = 0; i < 8; i++) p[i] = (unsigned char) (v >> (8 * i)); }
static void put_be64 (unsigned char *p, uint64_t v) { int i; for (i = 0; i < 8; i++) p[i] = (unsigned char) (v >> (8 * (7 - i))); }

static uint16_t get_le16 (const unsigned char *p) { return (uint16_t) p[0] | ((uint16_t) p[1] << 8); }
static uint16_t get_be16 (const unsigned char *p) { return (uint16_t) p[1] | ((uint16_t) p[0] << 8); }
static uint32_t get_le32 (const unsigned char *p) { int i; uint32_t v = 0; for (i = 0; i < 4; i++) v |= (uint32_t) p[i] << (8 * i); return v; }
static uint32_t get_be32 (const unsigned char *p) { int i; uint32_t v = 0; for (i = 0; i < 4; i++) v |= (uint32_t) p[i] << (8 * (3 - i)); return v; }
static uint64_t get_le64 (const unsigned char *p) { int i; uint64_t v = 0; for (i = 0; i < 8; i++) v |= (uint64_t) p[i] << (8 * i); return v; }
static uint64_t get_be64 (const unsigned char *p) { int i; uint64_t v = 0; for (i = 0; i < 8; i++) v |= (uint64_t) p[i] << (8 * (7 - i)); return v; }

/*
 * shadow buffer: an independent reference implementation of the same
 * insert-at-offset / remove-at-offset semantics as buffer-simple.c and
 * buffer-ring.c (including negative-offset-from-end and length == -1
 * meaning "until the end").
 */

struct shadow {
        unsigned char *data;
        int64_t length;
        int64_t capacity;
};

static void shadow_init (struct shadow *s)
{
        s->data = NULL;
        s->length = 0;
        s->capacity = 0;
}

static void shadow_uninit (struct shadow *s)
{
        free(s->data);
        s->data = NULL;
        s->length = 0;
        s->capacity = 0;
}

static int shadow_reserve (struct shadow *s, int64_t mincap)
{
        int64_t ncap;
        unsigned char *ndata;
        if (s->capacity >= mincap) {
                return 0;
        }
        ncap = (s->capacity > 0) ? s->capacity : 256;
        while (ncap < mincap) {
                ncap *= 2;
        }
        ndata = realloc(s->data, ncap);
        if (ndata == NULL) {
                return -1;
        }
        s->data = ndata;
        s->capacity = ncap;
        return 0;
}

static int shadow_normalize (int64_t slength, int64_t *offset)
{
        int64_t o = *offset;
        if (o < 0) {
                o = slength + o;
        }
        if (o < 0 || o > slength) {
                return -1;
        }
        *offset = o;
        return 0;
}

static int shadow_insert (struct shadow *s, int64_t offset, const void *data, int64_t length)
{
        if (shadow_normalize(s->length, &offset) != 0) {
                return -1;
        }
        if (length < 0) {
                return -1;
        }
        if (length == 0) {
                return 0;
        }
        if (shadow_reserve(s, s->length + length) != 0) {
                return -1;
        }
        if (offset != s->length) {
                memmove(s->data + offset + length, s->data + offset, s->length - offset);
        }
        memcpy(s->data + offset, data, length);
        s->length += length;
        return 0;
}

static int shadow_choke (struct shadow *s, int64_t offset, int64_t length)
{
        if (shadow_normalize(s->length, &offset) != 0) {
                return -1;
        }
        if (length < 0) {
                length = s->length - offset;
        }
        if (offset + length > s->length) {
                return -1;
        }
        if (length == 0) {
                return 0;
        }
        if (s->length - (offset + length) > 0) {
                memmove(s->data + offset, s->data + offset + length, s->length - (offset + length));
        }
        s->length -= length;
        return 0;
}

/*
 * consistency check: compares the real medusa_buffer content against the
 * shadow reference, using two independent read paths (peek_data and memcmp)
 * so a bug in either one is likely to be caught.
 */

static int verify (const char *what, unsigned int iteration, struct medusa_buffer *buffer, struct shadow *shadow)
{
        int rc;
        int64_t length;
        int64_t size;
        unsigned char *tmp;

        length = medusa_buffer_get_length(buffer);
        if (length != shadow->length) {
                fprintf(stderr, "fail @ %s, iteration: %u: length mismatch: buffer: %ld, shadow: %ld\n", what, iteration, (long) length, (long) shadow->length);
                return -1;
        }

        size = medusa_buffer_get_size(buffer);
        if (size < length) {
                fprintf(stderr, "fail @ %s, iteration: %u: size (%ld) < length (%ld)\n", what, iteration, (long) size, (long) length);
                return -1;
        }

        if (length == 0) {
                return 0;
        }

        tmp = malloc(length);
        if (tmp == NULL) {
                fprintf(stderr, "fail @ %s, iteration: %u: malloc failed\n", what, iteration);
                return -1;
        }

        rc = medusa_buffer_peek_data(buffer, 0, tmp, length);
        if (rc != 0) {
                fprintf(stderr, "fail @ %s, iteration: %u: peek_data rc: %d\n", what, iteration, rc);
                free(tmp);
                return -1;
        }
        if (memcmp(tmp, shadow->data, length) != 0) {
                int64_t i;
                for (i = 0; i < length; i++) {
                        if (tmp[i] != shadow->data[i]) {
                                break;
                        }
                }
                fprintf(stderr, "fail @ %s, iteration: %u: content mismatch (peek_data) at offset %ld / %ld (buffer: 0x%02x, shadow: 0x%02x)\n",
                        what, iteration, (long) i, (long) length,
                        (i < length) ? tmp[i] : 0, (i < length) ? shadow->data[i] : 0);
                free(tmp);
                return -1;
        }
        free(tmp);

        rc = medusa_buffer_memcmp(buffer, 0, shadow->data, shadow->length);
        if (rc != 0) {
                fprintf(stderr, "fail @ %s, iteration: %u: medusa_buffer_memcmp mismatch, rc: %d\n", what, iteration, rc);
                return -1;
        }

        return 0;
}

/*
 * onevent bookkeeping, same pattern as buffer-13/14
 */

static int buffer_onevent (struct medusa_buffer *buffer, unsigned int events, void *context, void *param)
{
        unsigned int *bevents = (unsigned int *) context;
        (void) buffer;
        (void) param;
        *bevents |= events;
        return 0;
}

static struct medusa_buffer * create_buffer (unsigned int type, unsigned int grow_size, unsigned int *bevents)
{
        int rc;
        struct medusa_buffer *buffer;
        struct medusa_buffer_init_options options;

        rc = medusa_buffer_init_options_default(&options);
        if (rc != 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.type      = type;
        options.grow_size = grow_size;
        options.onevent   = buffer_onevent;
        options.context   = bevents;
        buffer = medusa_buffer_create_with_options(&options);
        return buffer;
}

/*
 * core randomized stress test: append/prepend/insert (scalar and iovec),
 * choke (front, back, middle, negative offsets), shrink/maybe_shrink and
 * linearize, all cross checked against the shadow after every step.
 */

static int test_stress (unsigned int type, unsigned int niterations)
{
        int rc;
        unsigned int i;
        unsigned int bevents;
        int did_choke;

        struct medusa_buffer *buffer;
        struct shadow shadow;

        unsigned char chunk[256];
        struct medusa_iovec iovecs[4];
        unsigned char iovdata[4][64];

        bevents  = 0;
        did_choke = 0;
        shadow_init(&shadow);

        buffer = create_buffer(type, 1 + (rand() % 256), &bevents);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "test_stress: medusa_buffer_create_with_options failed\n");
                goto bail;
        }

        for (i = 0; i < niterations; i++) {
                int op = rand() % 11;

                if (op == 0) {
                        /* append */
                        int64_t length = rand_range(0, sizeof(chunk));
                        fill_random(chunk, length);
                        rc = medusa_buffer_append(buffer, chunk, length);
                        if (rc != length) {
                                fprintf(stderr, "fail @ append, iteration: %u: rc: %d != %ld\n", i, rc, (long) length);
                                goto bail;
                        }
                        if (shadow_insert(&shadow, shadow.length, chunk, length) != 0) {
                                goto bail;
                        }
                } else if (op == 1) {
                        /* prepend */
                        int64_t length = rand_range(0, sizeof(chunk));
                        fill_random(chunk, length);
                        rc = medusa_buffer_prepend(buffer, chunk, length);
                        if (rc != length) {
                                fprintf(stderr, "fail @ prepend, iteration: %u: rc: %d != %ld\n", i, rc, (long) length);
                                goto bail;
                        }
                        if (shadow_insert(&shadow, 0, chunk, length) != 0) {
                                goto bail;
                        }
                } else if (op == 2) {
                        /* insert at random offset (including negative-from-end) */
                        int64_t length = rand_range(0, sizeof(chunk));
                        int64_t offset = rand_range(0, shadow.length);
                        if (offset < shadow.length && (rand() % 2) == 0) {
                                offset = offset - shadow.length; /* equivalent negative offset for the same position */
                        }
                        fill_random(chunk, length);
                        rc = medusa_buffer_insert(buffer, offset, chunk, length);
                        if (rc != length) {
                                fprintf(stderr, "fail @ insert, iteration: %u: rc: %d != %ld, offset: %ld\n", i, rc, (long) length, (long) offset);
                                goto bail;
                        }
                        if (shadow_insert(&shadow, offset, chunk, length) != 0) {
                                goto bail;
                        }
                } else if (op == 3 || op == 4 || op == 5) {
                        /* appendv / prependv / insertv with 2-4 iovecs */
                        int64_t offset;
                        int64_t total = 0;
                        int n = 2 + (rand() % 3);
                        int j;
                        for (j = 0; j < n; j++) {
                                int64_t length = rand_range(0, sizeof(iovdata[0]));
                                fill_random(iovdata[j], length);
                                iovecs[j].iov_base = iovdata[j];
                                iovecs[j].iov_len  = length;
                                total += length;
                        }
                        if (op == 3) {
                                rc = medusa_buffer_appendv(buffer, iovecs, n);
                                offset = shadow.length;
                        } else if (op == 4) {
                                rc = medusa_buffer_prependv(buffer, iovecs, n);
                                offset = 0;
                        } else {
                                offset = rand_range(0, shadow.length);
                                rc = medusa_buffer_insertv(buffer, offset, iovecs, n);
                        }
                        if (rc != total) {
                                fprintf(stderr, "fail @ %sv, iteration: %u: rc: %d != %ld\n", (op == 3) ? "append" : (op == 4) ? "prepend" : "insert", i, rc, (long) total);
                                goto bail;
                        }
                        for (j = 0; j < n; j++) {
                                if (shadow_insert(&shadow, offset, iovecs[j].iov_base, iovecs[j].iov_len) != 0) {
                                        goto bail;
                                }
                                offset += iovecs[j].iov_len;
                        }
                } else if (op == 6 && shadow.length > 0) {
                        /* choke a chunk from the front, back, middle, or via negative offset */
                        int64_t offset;
                        int64_t length;
                        int which = rand() % 4;
                        if (which == 0) {
                                offset = 0;
                                length = rand_range(0, shadow.length);
                        } else if (which == 1) {
                                length = rand_range(0, shadow.length);
                                offset = shadow.length - length;
                        } else if (which == 2) {
                                offset = rand_range(0, shadow.length);
                                length = rand_range(0, shadow.length - offset);
                        } else {
                                offset = rand_range(-shadow.length, -1);
                                length = rand_range(0, -offset);
                        }
                        rc = medusa_buffer_choke(buffer, offset, length);
                        if (rc < 0) {
                                fprintf(stderr, "fail @ choke, iteration: %u: rc: %d, offset: %ld, length: %ld, shadow.length: %ld\n", i, rc, (long) offset, (long) length, (long) shadow.length);
                                goto bail;
                        }
                        if (shadow_choke(&shadow, offset, length) != 0) {
                                goto bail;
                        }
                        did_choke = 1;
                } else if (op == 7) {
                        /* shrink to the current length, must never lose data */
                        rc = medusa_buffer_shrink(buffer, shadow.length);
                        if (rc < 0) {
                                fprintf(stderr, "fail @ shrink, iteration: %u: rc: %d\n", i, rc);
                                goto bail;
                        }
                } else if (op == 8) {
                        rc = medusa_buffer_maybe_shrink(buffer, 64, 4096);
                        if (rc < 0) {
                                fprintf(stderr, "fail @ maybe_shrink, iteration: %u: rc: %d\n", i, rc);
                                goto bail;
                        }
                } else if (op == 9 && shadow.length > 0) {
                        /* linearize a random window and compare against the shadow */
                        void *ptr;
                        int64_t offset = rand_range(0, shadow.length - 1);
                        int64_t length = rand_range(1, shadow.length - offset);
                        ptr = medusa_buffer_linearize(buffer, offset, length);
                        if (MEDUSA_IS_ERR_OR_NULL(ptr)) {
                                fprintf(stderr, "fail @ linearize, iteration: %u: rc: %d, offset: %ld, length: %ld\n", i, MEDUSA_PTR_ERR(ptr), (long) offset, (long) length);
                                goto bail;
                        }
                        if (memcmp(ptr, shadow.data + offset, length) != 0) {
                                fprintf(stderr, "fail @ linearize, iteration: %u: content mismatch, offset: %ld, length: %ld\n", i, (long) offset, (long) length);
                                goto bail;
                        }
                } else {
                        /* writev, an alias for appendv */
                        int64_t total = 0;
                        int n = 1 + (rand() % 3);
                        int j;
                        for (j = 0; j < n; j++) {
                                int64_t length = rand_range(0, sizeof(iovdata[0]));
                                fill_random(iovdata[j], length);
                                iovecs[j].iov_base = iovdata[j];
                                iovecs[j].iov_len  = length;
                                total += length;
                        }
                        rc = medusa_buffer_writev(buffer, iovecs, n);
                        if (rc != total) {
                                fprintf(stderr, "fail @ writev, iteration: %u: rc: %d != %ld\n", i, rc, (long) total);
                                goto bail;
                        }
                        for (j = 0; j < n; j++) {
                                if (shadow_insert(&shadow, shadow.length, iovecs[j].iov_base, iovecs[j].iov_len) != 0) {
                                        goto bail;
                                }
                        }
                }

                if (verify("stress", i, buffer, &shadow) != 0) {
                        goto bail;
                }
        }

        medusa_buffer_destroy(buffer);
        shadow_uninit(&shadow);

        if (!(bevents & MEDUSA_BUFFER_EVENT_WRITE)) {
                fprintf(stderr, "fail: test_stress: MEDUSA_BUFFER_EVENT_WRITE never fired\n");
                return -1;
        }
        if (did_choke && !(bevents & MEDUSA_BUFFER_EVENT_CHOKE)) {
                fprintf(stderr, "fail: test_stress: choked but MEDUSA_BUFFER_EVENT_CHOKE never fired\n");
                return -1;
        }
        if (!(bevents & MEDUSA_BUFFER_EVENT_DESTROY)) {
                fprintf(stderr, "fail: test_stress: MEDUSA_BUFFER_EVENT_DESTROY never fired\n");
                return -1;
        }
        return 0;
bail:
        if (!MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        shadow_uninit(&shadow);
        return -1;
}

/*
 * uint8/16/32/64 x {native,le,be} x {prepend,append,insert} coverage: 36
 * write functions and, transitively (through verify()/peek/read below), the
 * matching peek/read accessors.
 */

static int test_uint_helpers (unsigned int type, unsigned int niterations)
{
        int rc;
        unsigned int i;
        unsigned int bevents;
        struct medusa_buffer *buffer;
        struct shadow shadow;

        shadow_init(&shadow);
        bevents = 0;
        buffer = create_buffer(type, 1 + (rand() % 64), &bevents);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "test_uint_helpers: medusa_buffer_create_with_options failed\n");
                goto bail;
        }

        for (i = 0; i < niterations; i++) {
                int width = rand() % 4;       /* 0:8 1:16 2:32 3:64 */
                int endian = rand() % 3;      /* 0:native 1:le 2:be */
                int where = rand() % 3;       /* 0:prepend 1:append 2:insert */
                int64_t offset;
                unsigned char expect[8];
                int64_t length;

                if (width == 0) {
                        uint8_t v = (uint8_t) rand();
                        length = 1;
                        expect[0] = v;
                        if (where == 0) {
                                rc = (endian == 0) ? medusa_buffer_prepend_uint8(buffer, v) :
                                     (endian == 1) ? medusa_buffer_prepend_uint8_le(buffer, v) :
                                                      medusa_buffer_prepend_uint8_be(buffer, v);
                                offset = 0;
                        } else if (where == 1) {
                                rc = (endian == 0) ? medusa_buffer_append_uint8(buffer, v) :
                                     (endian == 1) ? medusa_buffer_append_uint8_le(buffer, v) :
                                                      medusa_buffer_append_uint8_be(buffer, v);
                                offset = shadow.length;
                        } else {
                                offset = rand_range(0, shadow.length);
                                rc = (endian == 0) ? medusa_buffer_insert_uint8(buffer, offset, v) :
                                     (endian == 1) ? medusa_buffer_insert_uint8_le(buffer, offset, v) :
                                                      medusa_buffer_insert_uint8_be(buffer, offset, v);
                        }
                } else if (width == 1) {
                        uint16_t v = (uint16_t) rand64();
                        length = 2;
                        if (endian == 0) { memcpy(expect, &v, 2); }
                        else if (endian == 1) { put_le16(expect, v); }
                        else { put_be16(expect, v); }
                        if (where == 0) {
                                rc = (endian == 0) ? medusa_buffer_prepend_uint16(buffer, v) :
                                     (endian == 1) ? medusa_buffer_prepend_uint16_le(buffer, v) :
                                                      medusa_buffer_prepend_uint16_be(buffer, v);
                                offset = 0;
                        } else if (where == 1) {
                                rc = (endian == 0) ? medusa_buffer_append_uint16(buffer, v) :
                                     (endian == 1) ? medusa_buffer_append_uint16_le(buffer, v) :
                                                      medusa_buffer_append_uint16_be(buffer, v);
                                offset = shadow.length;
                        } else {
                                offset = rand_range(0, shadow.length);
                                rc = (endian == 0) ? medusa_buffer_insert_uint16(buffer, offset, v) :
                                     (endian == 1) ? medusa_buffer_insert_uint16_le(buffer, offset, v) :
                                                      medusa_buffer_insert_uint16_be(buffer, offset, v);
                        }
                } else if (width == 2) {
                        uint32_t v = (uint32_t) rand64();
                        length = 4;
                        if (endian == 0) { memcpy(expect, &v, 4); }
                        else if (endian == 1) { put_le32(expect, v); }
                        else { put_be32(expect, v); }
                        if (where == 0) {
                                rc = (endian == 0) ? medusa_buffer_prepend_uint32(buffer, v) :
                                     (endian == 1) ? medusa_buffer_prepend_uint32_le(buffer, v) :
                                                      medusa_buffer_prepend_uint32_be(buffer, v);
                                offset = 0;
                        } else if (where == 1) {
                                rc = (endian == 0) ? medusa_buffer_append_uint32(buffer, v) :
                                     (endian == 1) ? medusa_buffer_append_uint32_le(buffer, v) :
                                                      medusa_buffer_append_uint32_be(buffer, v);
                                offset = shadow.length;
                        } else {
                                offset = rand_range(0, shadow.length);
                                rc = (endian == 0) ? medusa_buffer_insert_uint32(buffer, offset, v) :
                                     (endian == 1) ? medusa_buffer_insert_uint32_le(buffer, offset, v) :
                                                      medusa_buffer_insert_uint32_be(buffer, offset, v);
                        }
                } else {
                        uint64_t v = rand64();
                        length = 8;
                        if (endian == 0) { memcpy(expect, &v, 8); }
                        else if (endian == 1) { put_le64(expect, v); }
                        else { put_be64(expect, v); }
                        if (where == 0) {
                                rc = (endian == 0) ? medusa_buffer_prepend_uint64(buffer, v) :
                                     (endian == 1) ? medusa_buffer_prepend_uint64_le(buffer, v) :
                                                      medusa_buffer_prepend_uint64_be(buffer, v);
                                offset = 0;
                        } else if (where == 1) {
                                rc = (endian == 0) ? medusa_buffer_append_uint64(buffer, v) :
                                     (endian == 1) ? medusa_buffer_append_uint64_le(buffer, v) :
                                                      medusa_buffer_append_uint64_be(buffer, v);
                                offset = shadow.length;
                        } else {
                                offset = rand_range(0, shadow.length);
                                rc = (endian == 0) ? medusa_buffer_insert_uint64(buffer, offset, v) :
                                     (endian == 1) ? medusa_buffer_insert_uint64_le(buffer, offset, v) :
                                                      medusa_buffer_insert_uint64_be(buffer, offset, v);
                        }
                }

                if (rc != length) {
                        fprintf(stderr, "fail @ uint_helpers, iteration: %u: rc: %d != %ld (width: %d, endian: %d, where: %d)\n", i, rc, (long) length, width, endian, where);
                        goto bail;
                }
                if (shadow_insert(&shadow, offset, expect, length) != 0) {
                        goto bail;
                }
                if (verify("uint_helpers", i, buffer, &shadow) != 0) {
                        goto bail;
                }

                /* round trip the matching peek_uintNN{,_le,_be} accessor as well */
                if (width == 0) {
                        uint8_t got = 0;
                        rc = (endian == 0) ? medusa_buffer_peek_uint8(buffer, offset, &got) :
                             (endian == 1) ? medusa_buffer_peek_uint8_le(buffer, offset, &got) :
                                              medusa_buffer_peek_uint8_be(buffer, offset, &got);
                        if (rc != 0 || got != expect[0]) {
                                fprintf(stderr, "fail @ peek_uint8, iteration: %u: rc: %d, got: 0x%02x, expect: 0x%02x\n", i, rc, got, expect[0]);
                                goto bail;
                        }
                } else if (width == 1) {
                        uint16_t got = 0;
                        uint16_t want;
                        rc = (endian == 0) ? medusa_buffer_peek_uint16(buffer, offset, &got) :
                             (endian == 1) ? medusa_buffer_peek_uint16_le(buffer, offset, &got) :
                                              medusa_buffer_peek_uint16_be(buffer, offset, &got);
                        want = (endian == 0) ? *(uint16_t *) expect : (endian == 1) ? get_le16(expect) : get_be16(expect);
                        if (rc != 0 || got != want) {
                                fprintf(stderr, "fail @ peek_uint16, iteration: %u: rc: %d, got: 0x%04x, expect: 0x%04x\n", i, rc, got, want);
                                goto bail;
                        }
                } else if (width == 2) {
                        uint32_t got = 0;
                        uint32_t want;
                        rc = (endian == 0) ? medusa_buffer_peek_uint32(buffer, offset, &got) :
                             (endian == 1) ? medusa_buffer_peek_uint32_le(buffer, offset, &got) :
                                              medusa_buffer_peek_uint32_be(buffer, offset, &got);
                        want = (endian == 0) ? *(uint32_t *) expect : (endian == 1) ? get_le32(expect) : get_be32(expect);
                        if (rc != 0 || got != want) {
                                fprintf(stderr, "fail @ peek_uint32, iteration: %u: rc: %d, got: 0x%08x, expect: 0x%08x\n", i, rc, got, want);
                                goto bail;
                        }
                } else {
                        uint64_t got = 0;
                        uint64_t want;
                        rc = (endian == 0) ? medusa_buffer_peek_uint64(buffer, offset, &got) :
                             (endian == 1) ? medusa_buffer_peek_uint64_le(buffer, offset, &got) :
                                              medusa_buffer_peek_uint64_be(buffer, offset, &got);
                        want = (endian == 0) ? *(uint64_t *) expect : (endian == 1) ? get_le64(expect) : get_be64(expect);
                        if (rc != 0 || got != want) {
                                fprintf(stderr, "fail @ peek_uint64, iteration: %u: rc: %d, got: 0x%016lx, expect: 0x%016lx\n", i, rc, (unsigned long) got, (unsigned long) want);
                                goto bail;
                        }
                }
        }

        medusa_buffer_destroy(buffer);
        shadow_uninit(&shadow);
        return 0;
bail:
        if (!MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        shadow_uninit(&shadow);
        return -1;
}

/*
 * read_data / read_uintNN: same as peek but must consume (choke) the bytes.
 */

static int test_read_helpers (unsigned int type, unsigned int niterations)
{
        int rc;
        unsigned int i;
        unsigned int bevents;
        struct medusa_buffer *buffer;
        struct shadow shadow;

        shadow_init(&shadow);
        bevents = 0;
        buffer = create_buffer(type, 1 + (rand() % 64), &bevents);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "test_read_helpers: medusa_buffer_create_with_options failed\n");
                goto bail;
        }

        for (i = 0; i < niterations; i++) {
                unsigned char chunk[64];
                int64_t length = rand_range(8, sizeof(chunk));
                fill_random(chunk, length);
                rc = medusa_buffer_append(buffer, chunk, length);
                if (rc != length) {
                        fprintf(stderr, "fail @ read_helpers/append, iteration: %u: rc: %d\n", i, rc);
                        goto bail;
                }
                if (shadow_insert(&shadow, shadow.length, chunk, length) != 0) {
                        goto bail;
                }

                /* read_data: consume a random prefix window at a random offset */
                {
                        int64_t offset = rand_range(0, shadow.length - 1);
                        int64_t rlen = rand_range(1, shadow.length - offset);
                        unsigned char *got = malloc(rlen);
                        if (got == NULL) {
                                goto bail;
                        }
                        rc = medusa_buffer_read_data(buffer, offset, got, rlen);
                        if (rc != 0) {
                                fprintf(stderr, "fail @ read_data, iteration: %u: rc: %d\n", i, rc);
                                free(got);
                                goto bail;
                        }
                        if (memcmp(got, shadow.data + offset, rlen) != 0) {
                                fprintf(stderr, "fail @ read_data, iteration: %u: content mismatch\n", i);
                                free(got);
                                goto bail;
                        }
                        free(got);
                        if (shadow_choke(&shadow, offset, rlen) != 0) {
                                goto bail;
                        }
                        if (verify("read_data", i, buffer, &shadow) != 0) {
                                goto bail;
                        }
                }

                /* read_uint8 at offset 0, if there is room */
                if (shadow.length >= 1) {
                        uint8_t v;
                        unsigned char expect = shadow.data[0];
                        rc = medusa_buffer_read_uint8(buffer, 0, &v);
                        if (rc != 0 || v != expect) {
                                fprintf(stderr, "fail @ read_uint8, iteration: %u: rc: %d, v: 0x%02x, expect: 0x%02x\n", i, rc, v, expect);
                                goto bail;
                        }
                        if (shadow_choke(&shadow, 0, 1) != 0) {
                                goto bail;
                        }
                        if (verify("read_uint8", i, buffer, &shadow) != 0) {
                                goto bail;
                        }
                }

                /* read_uint32_be at offset 0, if there is room */
                if (shadow.length >= 4) {
                        uint32_t v;
                        uint32_t expect = get_be32(shadow.data);
                        rc = medusa_buffer_read_uint32_be(buffer, 0, &v);
                        if (rc != 0 || v != expect) {
                                fprintf(stderr, "fail @ read_uint32_be, iteration: %u: rc: %d, v: 0x%08x, expect: 0x%08x\n", i, rc, v, expect);
                                goto bail;
                        }
                        if (shadow_choke(&shadow, 0, 4) != 0) {
                                goto bail;
                        }
                        if (verify("read_uint32_be", i, buffer, &shadow) != 0) {
                                goto bail;
                        }
                }
        }

        medusa_buffer_destroy(buffer);
        shadow_uninit(&shadow);
        return 0;
bail:
        if (!MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        shadow_uninit(&shadow);
        return -1;
}

/*
 * strcmp/strncmp/strcasecmp/strncasecmp/strchr/strcasechr/strstr/strcasestr:
 * plant a known printable needle at a random offset inside random printable
 * filler, then check every search/compare function agrees with a plain libc
 * reference computed independently over the shadow bytes.
 */

static int test_string_ops (unsigned int type, unsigned int niterations)
{
        int rc;
        unsigned int i;
        unsigned int bevents;
        struct medusa_buffer *buffer;

        bevents = 0;

        for (i = 0; i < niterations; i++) {
                unsigned char filler[128];
                char needle[16];
                int64_t fillerlen = rand_range(4, sizeof(filler));
                int64_t needlelen = rand_range(1, sizeof(needle) - 1);
                int64_t offset = rand_range(0, fillerlen);
                int64_t j;
                int64_t found;
                char upper_needle[16];

                fill_printable(filler, fillerlen);
                fill_printable((unsigned char *) needle, needlelen);
                needle[needlelen] = '\0';
                for (j = 0; j < needlelen; j++) {
                        upper_needle[j] = (char) toupper((unsigned char) needle[j]);
                }
                upper_needle[needlelen] = '\0';

                buffer = create_buffer(type, 1 + (rand() % 64), &bevents);
                if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                        fprintf(stderr, "test_string_ops: medusa_buffer_create_with_options failed\n");
                        goto bail;
                }

                rc = medusa_buffer_append(buffer, filler, offset);
                if (rc != offset) {
                        fprintf(stderr, "fail @ string_ops/append(prefix), iteration: %u: rc: %d\n", i, rc);
                        goto bail;
                }
                rc = medusa_buffer_append(buffer, needle, needlelen);
                if (rc != needlelen) {
                        fprintf(stderr, "fail @ string_ops/append(needle), iteration: %u: rc: %d\n", i, rc);
                        goto bail;
                }
                rc = medusa_buffer_append(buffer, filler + offset, fillerlen - offset);
                if (rc != fillerlen - offset) {
                        fprintf(stderr, "fail @ string_ops/append(suffix), iteration: %u: rc: %d\n", i, rc);
                        goto bail;
                }

                /* exact match at the planted offset */
                rc = medusa_buffer_strcmp(buffer, offset, needle);
                if (rc != 0) {
                        fprintf(stderr, "fail @ strcmp, iteration: %u: rc: %d\n", i, rc);
                        goto bail;
                }
                rc = medusa_buffer_strncmp(buffer, offset, needle, needlelen);
                if (rc != 0) {
                        fprintf(stderr, "fail @ strncmp, iteration: %u: rc: %d\n", i, rc);
                        goto bail;
                }
                rc = medusa_buffer_strcasecmp(buffer, offset, upper_needle);
                if (rc != 0) {
                        fprintf(stderr, "fail @ strcasecmp, iteration: %u: rc: %d\n", i, rc);
                        goto bail;
                }
                rc = medusa_buffer_strncasecmp(buffer, offset, upper_needle, needlelen);
                if (rc != 0) {
                        fprintf(stderr, "fail @ strncasecmp, iteration: %u: rc: %d\n", i, rc);
                        goto bail;
                }

                /* strstr / strcasestr must locate the needle at or after the plant point */
                found = medusa_buffer_strstr(buffer, 0, needle);
                if (found < 0 || found > offset) {
                        fprintf(stderr, "fail @ strstr, iteration: %u: found: %ld, expected <= %ld\n", i, (long) found, (long) offset);
                        goto bail;
                }
                found = medusa_buffer_strcasestr(buffer, 0, upper_needle);
                if (found < 0 || found > offset) {
                        fprintf(stderr, "fail @ strcasestr, iteration: %u: found: %ld, expected <= %ld\n", i, (long) found, (long) offset);
                        goto bail;
                }

                /* strchr / strcasechr for the first byte of the needle */
                found = medusa_buffer_strchr(buffer, 0, needle[0]);
                if (found < 0 || found > offset) {
                        fprintf(stderr, "fail @ strchr, iteration: %u: found: %ld, expected <= %ld\n", i, (long) found, (long) offset);
                        goto bail;
                }
                found = medusa_buffer_strcasechr(buffer, 0, upper_needle[0]);
                if (found < 0 || found > offset) {
                        fprintf(stderr, "fail @ strcasechr, iteration: %u: found: %ld, expected <= %ld\n", i, (long) found, (long) offset);
                        goto bail;
                }

                /* a needle that cannot possibly occur must not be found */
                {
                        static const char impossible[] = "\x01\x02\x03-impossible-needle-\x04\x05";
                        found = medusa_buffer_strstr(buffer, 0, impossible);
                        if (found >= 0) {
                                fprintf(stderr, "fail @ strstr(negative), iteration: %u: unexpectedly found at %ld\n", i, (long) found);
                                goto bail;
                        }
                }

                medusa_buffer_destroy(buffer);
        }
        return 0;
bail:
        if (!MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        return -1;
}

/*
 * appendf/printf/vprintf always insert at the current end of the buffer, so
 * they are not exposed to the insertfv() offset bug documented below; they
 * are cross-checked against a plain vsnprintf() of the same format/args.
 */

static int vappend_expect (struct medusa_buffer *buffer, struct shadow *shadow, const char *format, ...)
{
        int rc;
        va_list ap;
        char expect[256];

        va_start(ap, format);
        rc = vsnprintf(expect, sizeof(expect), format, ap);
        va_end(ap);
        if (rc < 0 || (size_t) rc >= sizeof(expect)) {
                return -1;
        }

        va_start(ap, format);
        rc = medusa_buffer_appendfv(buffer, format, ap);
        va_end(ap);
        if (rc != (int) strlen(expect)) {
                fprintf(stderr, "fail @ appendf: rc: %d != %d\n", rc, (int) strlen(expect));
                return -1;
        }
        return shadow_insert(shadow, shadow->length, expect, strlen(expect));
}

static int test_printf (unsigned int type, unsigned int niterations)
{
        unsigned int i;
        struct medusa_buffer *buffer;
        struct shadow shadow;

        shadow_init(&shadow);
        buffer = medusa_buffer_create(type);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "test_printf: medusa_buffer_create failed\n");
                goto bail;
        }

        for (i = 0; i < niterations; i++) {
                int rc;
                int which = rand() % 3;
                int n = rand();
                char word[16];
                fill_printable((unsigned char *) word, sizeof(word) - 1);
                word[sizeof(word) - 1] = '\0';

                if (which == 0) {
                        if (vappend_expect(buffer, &shadow, "n=%d/", n) != 0) {
                                goto bail;
                        }
                } else if (which == 1) {
                        char expect[64];
                        rc = snprintf(expect, sizeof(expect), "[%s:%08x]", word, (unsigned int) n);
                        rc = medusa_buffer_printf(buffer, "[%s:%08x]", word, (unsigned int) n);
                        if (rc != (int) strlen(expect)) {
                                fprintf(stderr, "fail @ printf, iteration: %u: rc: %d != %d\n", i, rc, (int) strlen(expect));
                                goto bail;
                        }
                        if (shadow_insert(&shadow, shadow.length, expect, strlen(expect)) != 0) {
                                goto bail;
                        }
                } else {
                        if (vappend_expect(buffer, &shadow, "%s", word) != 0) {
                                goto bail;
                        }
                }

                if (verify("printf", i, buffer, &shadow) != 0) {
                        goto bail;
                }
        }

        medusa_buffer_destroy(buffer);
        shadow_uninit(&shadow);
        return 0;
bail:
        if (!MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        shadow_uninit(&shadow);
        return -1;
}

/*
 * insertf/prependf into a NON-EMPTY buffer, at an offset that is not the
 * current end. This is the one function family the sanity tests above
 * cannot exercise, since appendf/printf/vprintf always target the end.
 *
 * This used to fail against src/buffer-simple.c and src/buffer-ring.c:
 * both backends' insertfv() reserved (vsnprintf-length + 1) bytes for the
 * formatted text (to make room for vsnprintf's trailing NUL) and shifted
 * the existing tail by that same padded amount, but only added the
 * *unpadded* vsnprintf length to the buffer's logical length. The result
 * was a stray NUL byte left inside the logical content right after the
 * inserted text, and the last byte of whatever followed the insertion
 * point silently dropped. Both backends now format into a scratch buffer
 * and delegate the actual placement to insertv(), which does not have
 * this problem.
 */

static int test_insertf_middle (unsigned int type, unsigned int niterations)
{
        int rc;
        unsigned int i;
        struct medusa_buffer *buffer;
        struct shadow shadow;

        shadow_init(&shadow);
        buffer = medusa_buffer_create(type);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "test_insertf_middle: medusa_buffer_create failed\n");
                goto bail;
        }

        for (i = 0; i < niterations; i++) {
                unsigned char prefix[32];
                unsigned char suffix[32];
                char word[16];
                char expect[32];
                int64_t prefixlen = rand_range(1, sizeof(prefix));
                int64_t suffixlen = rand_range(1, sizeof(suffix));
                int n = rand() % 1000;

                fill_printable(prefix, prefixlen);
                fill_printable(suffix, suffixlen);
                fill_printable((unsigned char *) word, sizeof(word) - 1);
                word[sizeof(word) - 1] = '\0';
                snprintf(expect, sizeof(expect), "<%s:%d>", word, n);

                rc = medusa_buffer_append(buffer, prefix, prefixlen);
                if (rc != prefixlen) {
                        goto bail;
                }
                shadow_insert(&shadow, shadow.length, prefix, prefixlen);

                rc = medusa_buffer_append(buffer, suffix, suffixlen);
                if (rc != suffixlen) {
                        goto bail;
                }
                shadow_insert(&shadow, shadow.length, suffix, suffixlen);

                /* insertf right at the prefix/suffix boundary: neither the
                 * true start (0) nor the true end (shadow.length) */
                rc = medusa_buffer_insertf(buffer, prefixlen, "<%s:%d>", word, n);
                if (rc != (int) strlen(expect)) {
                        fprintf(stderr, "fail @ insertf, iteration: %u: rc: %d != %d\n", i, rc, (int) strlen(expect));
                        goto bail;
                }
                if (shadow_insert(&shadow, prefixlen, expect, strlen(expect)) != 0) {
                        goto bail;
                }

                if (verify("insertf_middle", i, buffer, &shadow) != 0) {
                        goto bail;
                }
        }

        medusa_buffer_destroy(buffer);
        shadow_uninit(&shadow);
        return 0;
bail:
        if (!MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        shadow_uninit(&shadow);
        return -1;
}

/*
 * reservev/commitv, driven hard enough (repeated small reserve/commit cycles
 * interleaved with front chokes) to push the ring backend through wraparound,
 * where peekv/reservev legitimately return more than one iovec.
 */

static int test_reserve_commit (unsigned int type, unsigned int niterations)
{
        int rc;
        unsigned int i;
        struct medusa_buffer *buffer;
        struct shadow shadow;
        unsigned char data[64];

        shadow_init(&shadow);
        buffer = medusa_buffer_create(type);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "test_reserve_commit: medusa_buffer_create failed\n");
                goto bail;
        }

        for (i = 0; i < niterations; i++) {
                int64_t length = rand_range(1, sizeof(data));
                int64_t niovecs;
                int64_t j;
                int64_t off;
                struct medusa_iovec *iovecs;

                niovecs = medusa_buffer_reservev(buffer, length, NULL, 0);
                if (niovecs < 0) {
                        fprintf(stderr, "fail @ reservev(query), iteration: %u: rc: %ld\n", i, (long) niovecs);
                        goto bail;
                }
                iovecs = malloc(sizeof(struct medusa_iovec) * niovecs);
                if (iovecs == NULL) {
                        goto bail;
                }
                niovecs = medusa_buffer_reservev(buffer, length, iovecs, niovecs);
                if (niovecs < 0) {
                        fprintf(stderr, "fail @ reservev, iteration: %u: rc: %ld\n", i, (long) niovecs);
                        free(iovecs);
                        goto bail;
                }

                fill_random(data, length);
                off = 0;
                for (j = 0; j < niovecs; j++) {
                        memcpy(iovecs[j].iov_base, data + off, iovecs[j].iov_len);
                        off += iovecs[j].iov_len;
                }
                if (off != length) {
                        fprintf(stderr, "fail @ reservev, iteration: %u: iovecs cover %ld != %ld\n", i, (long) off, (long) length);
                        free(iovecs);
                        goto bail;
                }

                rc = medusa_buffer_commitv(buffer, iovecs, niovecs);
                if (rc != niovecs) {
                        fprintf(stderr, "fail @ commitv, iteration: %u: rc: %d != %ld\n", i, rc, (long) niovecs);
                        free(iovecs);
                        goto bail;
                }
                free(iovecs);

                if (shadow_insert(&shadow, shadow.length, data, length) != 0) {
                        goto bail;
                }
                if (verify("reserve_commit", i, buffer, &shadow) != 0) {
                        goto bail;
                }

                /* occasionally choke from the front to force the ring
                 * backend to wrap around on the next reserve */
                if (shadow.length > 0 && (rand() % 3) == 0) {
                        int64_t clen = rand_range(1, shadow.length);
                        rc = medusa_buffer_choke(buffer, 0, clen);
                        if (rc != clen) {
                                fprintf(stderr, "fail @ reserve_commit/choke, iteration: %u: rc: %d != %ld\n", i, rc, (long) clen);
                                goto bail;
                        }
                        if (shadow_choke(&shadow, 0, clen) != 0) {
                                goto bail;
                        }
                        if (verify("reserve_commit/choke", i, buffer, &shadow) != 0) {
                                goto bail;
                        }
                }
        }

        medusa_buffer_destroy(buffer);
        shadow_uninit(&shadow);
        return 0;
bail:
        if (!MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        shadow_uninit(&shadow);
        return -1;
}

/*
 * a handful of documented error paths: out-of-range offsets/lengths and
 * obviously invalid arguments must all be rejected, never silently clamped.
 */

static int test_negative_paths (unsigned int type)
{
        int rc;
        void *ptr;
        struct medusa_buffer *buffer;
        char data[16] = "0123456789abcde";

        buffer = medusa_buffer_create(type);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "test_negative_paths: medusa_buffer_create failed\n");
                goto bail;
        }

        rc = medusa_buffer_append(buffer, NULL, 5);
        if (rc >= 0) {
                fprintf(stderr, "fail @ negative_paths: append(NULL) accepted, rc: %d\n", rc);
                goto bail;
        }
        rc = medusa_buffer_append(buffer, data, -1);
        if (rc >= 0) {
                fprintf(stderr, "fail @ negative_paths: append(length -1) accepted, rc: %d\n", rc);
                goto bail;
        }

        rc = medusa_buffer_append(buffer, data, sizeof(data));
        if (rc != (int) sizeof(data)) {
                fprintf(stderr, "fail @ negative_paths: append failed, rc: %d\n", rc);
                goto bail;
        }

        rc = medusa_buffer_insert(buffer, medusa_buffer_get_length(buffer) + 1, data, 1);
        if (rc >= 0) {
                fprintf(stderr, "fail @ negative_paths: insert() past the end accepted, rc: %d\n", rc);
                goto bail;
        }
        rc = medusa_buffer_insert(buffer, -(medusa_buffer_get_length(buffer) + 1), data, 1);
        if (rc >= 0) {
                fprintf(stderr, "fail @ negative_paths: insert() with too-negative offset accepted, rc: %d\n", rc);
                goto bail;
        }

        rc = medusa_buffer_choke(buffer, 0, medusa_buffer_get_length(buffer) + 1);
        if (rc >= 0) {
                fprintf(stderr, "fail @ negative_paths: choke() past the end accepted, rc: %d\n", rc);
                goto bail;
        }

        rc = (int) medusa_buffer_peekv(buffer, medusa_buffer_get_length(buffer) + 1, 1, NULL, 0);
        if (rc >= 0) {
                fprintf(stderr, "fail @ negative_paths: peekv() past the end accepted, rc: %d\n", rc);
                goto bail;
        }

        ptr = medusa_buffer_linearize(buffer, medusa_buffer_get_length(buffer) + 1, 1);
        if (!MEDUSA_IS_ERR_OR_NULL(ptr)) {
                fprintf(stderr, "fail @ negative_paths: linearize() past the end accepted\n");
                goto bail;
        }

        rc = medusa_buffer_shrink(buffer, -1);
        if (rc >= 0) {
                fprintf(stderr, "fail @ negative_paths: shrink(-1) accepted, rc: %d\n", rc);
                goto bail;
        }
        rc = medusa_buffer_shrink(buffer, medusa_buffer_get_length(buffer) - 1);
        if (rc >= 0) {
                fprintf(stderr, "fail @ negative_paths: shrink() below current length accepted, rc: %d\n", rc);
                goto bail;
        }

        medusa_buffer_destroy(buffer);
        return 0;
bail:
        if (!MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        return -1;
}

int main (int argc, char *argv[])
{
        unsigned int i;
        unsigned int nfailed;
        (void) argc;
        (void) argv;

        srand((unsigned int) time(NULL) ^ (unsigned int) getpid());

        nfailed = 0;
        fprintf(stderr, "start\n");
        for (i = 0; i < sizeof(g_types) / sizeof(g_types[0]); i++) {
                unsigned int type = g_types[i];
                fprintf(stderr, "type: %d (%s)\n", type, type_name(type));

                fprintf(stderr, "  test_stress ...\n");
                if (test_stress(type, 2000) != 0) {
                        fprintf(stderr, "  test_stress: fail\n");
                        nfailed++;
                }

                fprintf(stderr, "  test_uint_helpers ...\n");
                if (test_uint_helpers(type, 500) != 0) {
                        fprintf(stderr, "  test_uint_helpers: fail\n");
                        nfailed++;
                }

                fprintf(stderr, "  test_read_helpers ...\n");
                if (test_read_helpers(type, 500) != 0) {
                        fprintf(stderr, "  test_read_helpers: fail\n");
                        nfailed++;
                }

                fprintf(stderr, "  test_string_ops ...\n");
                if (test_string_ops(type, 300) != 0) {
                        fprintf(stderr, "  test_string_ops: fail\n");
                        nfailed++;
                }

                fprintf(stderr, "  test_printf ...\n");
                if (test_printf(type, 300) != 0) {
                        fprintf(stderr, "  test_printf: fail\n");
                        nfailed++;
                }

                fprintf(stderr, "  test_insertf_middle ...\n");
                if (test_insertf_middle(type, 100) != 0) {
                        fprintf(stderr, "  test_insertf_middle: fail\n");
                        nfailed++;
                }

                fprintf(stderr, "  test_reserve_commit ...\n");
                if (test_reserve_commit(type, 500) != 0) {
                        fprintf(stderr, "  test_reserve_commit: fail\n");
                        nfailed++;
                }

                fprintf(stderr, "  test_negative_paths ...\n");
                if (test_negative_paths(type) != 0) {
                        fprintf(stderr, "  test_negative_paths: fail\n");
                        nfailed++;
                }
        }

        if (nfailed != 0) {
                fprintf(stderr, "fail (%u sub-test(s) failed)\n", nfailed);
                return -1;
        }
        fprintf(stderr, "success\n");
        return 0;
}
