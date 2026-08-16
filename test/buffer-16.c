
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

#include "medusa/error.h"
#include "medusa/iovec.h"
#include "medusa/buffer.h"

/*
 * buffer-16: stress the search / compare family in buffer.h
 *
 *   medusa_buffer_memcmp        medusa_buffer_memmem
 *   medusa_buffer_strcmp        medusa_buffer_strstr
 *   medusa_buffer_strncmp       medusa_buffer_strcasestr
 *   medusa_buffer_strcasecmp    medusa_buffer_strchr
 *   medusa_buffer_strncasecmp   medusa_buffer_strcasechr
 *
 * Every call is cross-checked against a reference implementation running on a
 * flat array, so the backends have to agree with plain C semantics rather than
 * merely with each other.
 *
 * Three properties are specifically targeted.
 *
 *   - The compare family is a *prefix* compare: it looks at
 *     min(available, needle) bytes and reports 0 when the needle is a prefix
 *     of the buffer tail. strstr/strcasestr depend on that, since they probe
 *     every offset with the comparator.
 *
 *   - offset < 0 counts back from the end, the same convention peekv uses.
 *     The search functions have to resolve that themselves; leaving it to the
 *     comparator makes the probe positions walk backwards and lets a negative
 *     index be reported as a hit.
 *
 *   - The ring backend returns two iovecs whenever the inspected region
 *     straddles the end of the allocation. build_content() puts the
 *     wraparound at a chosen byte of the content and asserts it landed there,
 *     and the run prints how many probes actually saw a split so that a change
 *     which silently stops wrapping cannot quietly drop the coverage.
 *
 * str* haystacks and needles are kept NUL free. strncasecmp() stops at a NUL
 * and memcmp() does not, so mixing them would measure both against a reference
 * that cannot describe either. Embedded NULs are covered through the mem*
 * entry points and through the strchr()/strcasechr() NUL cases.
 */

static unsigned int g_probe_count;
static unsigned int g_split_seen;

/* ------------------------------------------------------------------ prng */

static uint64_t g_rand_state = 0x123456789abcdefull;

static uint64_t xorshift (void)
{
        g_rand_state ^= g_rand_state << 13;
        g_rand_state ^= g_rand_state >> 7;
        g_rand_state ^= g_rand_state << 17;
        return g_rand_state;
}

static int64_t rnd (int64_t n)
{
        if (n <= 0) {
                return 0;
        }
        return (int64_t) (xorshift() % (uint64_t) n);
}

/* ------------------------------------------------------------- reference */

/*
 * prefix compare of ndl against hay[offset ...]: compare what is there, and
 * treat a haystack that runs out early as a shorter prefix, hence less.
 */
static int ref_compare (const unsigned char *hay, int64_t haylen, int64_t offset, const unsigned char *ndl, int64_t ndllen, int nocase)
{
        int64_t i;
        int64_t avail;
        int64_t n;

        if (offset < 0) {
                offset = haylen + offset;
        }
        if (offset < 0 ||
            offset > haylen) {
                return -EINVAL;
        }
        if (ndllen == 0) {
                return 0;
        }

        avail = haylen - offset;
        n     = (avail < ndllen) ? avail : ndllen;
        for (i = 0; i < n; i++) {
                int a = hay[offset + i];
                int b = ndl[i];
                if (nocase) {
                        a = tolower(a);
                        b = tolower(b);
                }
                if (a != b) {
                        return (a < b) ? -1 : 1;
                }
        }
        if (avail < ndllen) {
                return -1;
        }
        return 0;
}

static int64_t ref_search (const unsigned char *hay, int64_t haylen, int64_t offset, const unsigned char *ndl, int64_t ndllen, int nocase)
{
        int64_t i;

        if (offset < 0) {
                offset = haylen + offset;
        }
        if (offset < 0 ||
            offset > haylen) {
                return -EINVAL;
        }
        if (ndllen == 0) {
                return offset;
        }
        for (i = offset; i + ndllen <= haylen; i++) {
                if (ref_compare(hay, haylen, i, ndl, ndllen, nocase) == 0) {
                        return i;
                }
        }
        return -ENOENT;
}

static int sgn (int64_t v)
{
        return (v > 0) - (v < 0);
}

/* ---------------------------------------------------------- construction */

struct config {
        unsigned int type;
        const char *name;
        unsigned int grow;
        unsigned int flags;
        /* 0: leave the content linear. -1: wrap at the middle. k: wrap k bytes in. */
        int wrap_at;
};

static const struct config g_configs[] = {
        { MEDUSA_BUFFER_TYPE_SIMPLE, "simple",           1024, MEDUSA_BUFFER_FLAG_NONE,        0 },
        { MEDUSA_BUFFER_TYPE_SIMPLE, "simple/shrink",      16, MEDUSA_BUFFER_FLAG_SHRINKABLE,  0 },
        { MEDUSA_BUFFER_TYPE_RING,   "ring",             1024, MEDUSA_BUFFER_FLAG_NONE,        0 },
        { MEDUSA_BUFFER_TYPE_RING,   "ring/shrink",        16, MEDUSA_BUFFER_FLAG_SHRINKABLE,  0 },
        { MEDUSA_BUFFER_TYPE_RING,   "ring/wrap-mid",      16, MEDUSA_BUFFER_FLAG_NONE,       -1 },
        { MEDUSA_BUFFER_TYPE_RING,   "ring/wrap-1",        16, MEDUSA_BUFFER_FLAG_NONE,        1 },
        { MEDUSA_BUFFER_TYPE_RING,   "ring/wrap-3",         8, MEDUSA_BUFFER_FLAG_NONE,        3 }
};

/* every grow size above divides this, so the ring allocation is exactly this big */
#define BUILD_CAPACITY  1024
#define BUILD_MAX       192

static struct medusa_buffer * buffer_create (const struct config *config)
{
        int rc;
        struct medusa_buffer_init_options options;

        rc = medusa_buffer_init_options_default(&options);
        if (rc < 0) {
                return NULL;
        }
        options.type        = config->type;
        options.flags       = config->flags;
        options.grow_size   = config->grow;
        options.shrink_size = config->grow;
        return medusa_buffer_create_with_options(&options);
}

static int64_t wrap_point (int64_t length, int wrap_at)
{
        if (wrap_at == 0 ||
            length < 2) {
                return 0;
        }
        if (wrap_at < 0) {
                return length / 2;
        }
        return (wrap_at < length) ? wrap_at : length - 1;
}

/*
 * install exactly `content`, positioned so that content byte `wp` sits at
 * physical offset 0 of the ring allocation - that is, the wraparound falls
 * inside the content at index wp. returns wp, or 0 for a linear layout.
 *
 * two behaviours of the ring backend shape this. ring_buffer_resize() calls
 * ring_buffer_headify(), which linearizes and rewinds head to 0, so the
 * allocation must reach its final size before head is moved and nothing after
 * that may grow it. and choking the entire content also rewinds head, so at
 * least one byte stays in the buffer throughout.
 *
 * head advances by exactly one per choke(1)/append(1) cycle, so the landing
 * position can be dialled in precisely even though head is not observable.
 * the handover below shifts head by a further length + 1, which is accounted
 * for when picking the cycle count.
 */
static int64_t build_content (const struct config *config, struct medusa_buffer *buffer, const unsigned char *content, int64_t length)
{
        int64_t i;
        int64_t rc;
        int64_t wp;
        int64_t want_head;
        unsigned char filler[BUILD_CAPACITY];

        memset(filler, 'x', sizeof(filler));

        if (length + 1 > BUILD_CAPACITY) {
                return -1;
        }
        wp = wrap_point(length, config->wrap_at);

        /* grow the allocation to its final size, then empty it again */
        rc = medusa_buffer_append(buffer, filler, BUILD_CAPACITY);
        if (rc != BUILD_CAPACITY) {
                return -1;
        }
        rc = medusa_buffer_choke(buffer, 0, BUILD_CAPACITY);
        if (rc != BUILD_CAPACITY) {
                return -1;
        }

        rc = medusa_buffer_append(buffer, filler, length + 1);
        if (rc != length + 1) {
                return -1;
        }
        if (wp > 0) {
                want_head = (BUILD_CAPACITY - wp - (length + 1)) % BUILD_CAPACITY;
                if (want_head < 0) {
                        want_head += BUILD_CAPACITY;
                }
                for (i = 0; i < want_head; i++) {
                        rc = medusa_buffer_choke(buffer, 0, 1);
                        if (rc != 1) {
                                return -1;
                        }
                        rc = medusa_buffer_append(buffer, filler, 1);
                        if (rc != 1) {
                                return -1;
                        }
                }
        }

        /* hand over to the real content, still without ever emptying the buffer */
        if (length > 0) {
                rc = medusa_buffer_choke(buffer, 0, length);
                if (rc != length) {
                        return -1;
                }
                rc = medusa_buffer_append(buffer, content, length);
                if (rc != length) {
                        return -1;
                }
        }
        rc = medusa_buffer_choke(buffer, 0, 1);
        if (rc != 1) {
                return -1;
        }

        if (medusa_buffer_get_length(buffer) != length) {
                return -1;
        }
        if (length > 0 &&
            medusa_buffer_memcmp(buffer, 0, content, length) != 0) {
                return -1;
        }
        if (wp > 0) {
                /* the two byte window across wp must come back as two iovecs */
                if (medusa_buffer_peekv(buffer, wp - 1, 2, NULL, 0) != 2) {
                        fprintf(stderr, "fail @ %s: content does not wrap at %ld as intended\n",
                                config->name, (long) wp);
                        return -1;
                }
        }
        return wp;
}

static void note_probe (struct medusa_buffer *buffer, int64_t offset, int64_t length)
{
        g_probe_count++;
        if (length <= 0 ||
            offset < 0) {
                return;
        }
        if (medusa_buffer_peekv(buffer, offset, length, NULL, 0) > 1) {
                g_split_seen++;
        }
}

/* ----------------------------------------------------------------- probe */

/*
 * run all ten entry points at one (haystack, needle, offset) point and check
 * every result against the reference.
 */
static int probe (const struct config *config, struct medusa_buffer *buffer,
                  const unsigned char *hay, int64_t haylen, int64_t offset,
                  const unsigned char *ndl, int64_t ndllen, int strsafe)
{
        int rc;
        int want;
        int64_t rc64;
        int64_t want64;
        int nfailed;
        char ndlstr[BUILD_MAX + 1];

        nfailed = 0;

        note_probe(buffer, offset, ndllen);

#define CHECK_CMP(call, expect, what)                                                           \
        do {                                                                                    \
                rc   = (call);                                                                  \
                want = (expect);                                                                \
                if (sgn(rc) != sgn(want)) {                                                     \
                        fprintf(stderr, "fail @ %s/%s: haylen: %ld, offset: %ld, ndllen: %ld,"  \
                                        " got: %d (sgn %d), want sgn %d\n",                     \
                                config->name, what, (long) haylen, (long) offset,               \
                                (long) ndllen, rc, sgn(rc), sgn(want));                         \
                        nfailed++;                                                              \
                }                                                                               \
        } while (0)

#define CHECK_SEARCH(call, expect, what)                                                        \
        do {                                                                                    \
                rc64   = (call);                                                                \
                want64 = (expect);                                                              \
                if (rc64 != want64) {                                                           \
                        fprintf(stderr, "fail @ %s/%s: haylen: %ld, offset: %ld, ndllen: %ld,"  \
                                        " got: %ld, want: %ld\n",                               \
                                config->name, what, (long) haylen, (long) offset,               \
                                (long) ndllen, (long) rc64, (long) want64);                     \
                        nfailed++;                                                              \
                }                                                                               \
        } while (0)

        CHECK_CMP(medusa_buffer_memcmp(buffer, offset, ndl, ndllen),
                  ref_compare(hay, haylen, offset, ndl, ndllen, 0),
                  "memcmp");
        CHECK_SEARCH(medusa_buffer_memmem(buffer, offset, ndl, ndllen),
                     ref_search(hay, haylen, offset, ndl, ndllen, 0),
                     "memmem");

        if (!strsafe ||
            ndllen > BUILD_MAX) {
                goto out;
        }
        memcpy(ndlstr, ndl, ndllen);
        ndlstr[ndllen] = '\0';

        CHECK_CMP(medusa_buffer_strcmp(buffer, offset, ndlstr),
                  ref_compare(hay, haylen, offset, ndl, ndllen, 0),
                  "strcmp");
        CHECK_CMP(medusa_buffer_strcasecmp(buffer, offset, ndlstr),
                  ref_compare(hay, haylen, offset, ndl, ndllen, 1),
                  "strcasecmp");
        CHECK_SEARCH(medusa_buffer_strstr(buffer, offset, ndlstr),
                     ref_search(hay, haylen, offset, ndl, ndllen, 0),
                     "strstr");
        CHECK_SEARCH(medusa_buffer_strcasestr(buffer, offset, ndlstr),
                     ref_search(hay, haylen, offset, ndl, ndllen, 1),
                     "strcasestr");

        /* strncmp / strncasecmp over every truncation of the needle */
        {
                int64_t n;
                for (n = 0; n <= ndllen + 1; n++) {
                        int64_t clipped = (n < ndllen) ? n : ndllen;
                        CHECK_CMP(medusa_buffer_strncmp(buffer, offset, ndlstr, n),
                                  ref_compare(hay, haylen, offset, ndl, clipped, 0),
                                  "strncmp");
                        CHECK_CMP(medusa_buffer_strncasecmp(buffer, offset, ndlstr, n),
                                  ref_compare(hay, haylen, offset, ndl, clipped, 1),
                                  "strncasecmp");
                }
        }

        /* strchr / strcasechr against the first needle byte */
        if (ndllen > 0) {
                unsigned char c = ndl[0];
                CHECK_SEARCH(medusa_buffer_strchr(buffer, offset, (char) c),
                             ref_search(hay, haylen, offset, &c, 1, 0),
                             "strchr");
                CHECK_SEARCH(medusa_buffer_strcasechr(buffer, offset, (char) c),
                             ref_search(hay, haylen, offset, &c, 1, 1),
                             "strcasechr");
        }

out:
        return nfailed;

#undef CHECK_CMP
#undef CHECK_SEARCH
}

/* probe every offset in [lo, hi], clamped to the buffer */
static int probe_range (const struct config *config, struct medusa_buffer *buffer,
                        const unsigned char *hay, int64_t haylen, int64_t lo, int64_t hi,
                        const unsigned char *ndl, int64_t ndllen)
{
        int64_t o;
        int nfailed;

        nfailed = 0;
        if (lo < 0) {
                lo = 0;
        }
        if (hi > haylen) {
                hi = haylen;
            }
        for (o = lo; o <= hi; o++) {
                nfailed += probe(config, buffer, hay, haylen, o, ndl, ndllen, 1);
        }
        return nfailed;
}

/* ----------------------------------------------------------------- tests */

/*
 * randomized differential sweep. haystacks come from a small alphabet so that
 * partial matches and repeated prefixes are common, which is where the offset
 * bookkeeping in the search loops tends to break.
 */
static int test_random (const struct config *config, unsigned int iterations)
{
        unsigned int it;
        int nfailed;
        unsigned char hay[BUILD_MAX];
        unsigned char ndl[16];

        nfailed = 0;

        for (it = 0; it < iterations; it++) {
                int64_t i;
                int64_t wp;
                int64_t haylen;
                int64_t ndllen;
                int64_t offset;
                struct medusa_buffer *buffer;

                haylen = rnd((int64_t) sizeof(hay) + 1);
                ndllen = rnd((int64_t) sizeof(ndl) + 1);

                for (i = 0; i < haylen; i++) {
                        hay[i] = (unsigned char) ("aAbBcC"[rnd(6)]);
                }
                for (i = 0; i < ndllen; i++) {
                        ndl[i] = (unsigned char) ("aAbBcC"[rnd(6)]);
                }
                /* half the time plant the needle so that hits are common */
                if (ndllen > 0 &&
                    haylen >= ndllen &&
                    rnd(2) == 0) {
                        memcpy(hay + rnd(haylen - ndllen + 1), ndl, ndllen);
                }

                buffer = buffer_create(config);
                if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                        fprintf(stderr, "fail @ %s: can not create buffer\n", config->name);
                        return -1;
                }
                wp = build_content(config, buffer, hay, haylen);
                if (wp < 0) {
                        medusa_buffer_destroy(buffer);
                        return -1;
                }

                /* scattered offsets over the whole buffer */
                for (offset = 0; offset <= haylen; offset += 1 + rnd(7)) {
                        nfailed += probe(config, buffer, hay, haylen, offset, ndl, ndllen, 1);
                }
                /* and every offset whose needle window can cross the wraparound */
                if (wp > 0) {
                        nfailed += probe_range(config, buffer, hay, haylen,
                                               wp - ndllen - 1, wp + 1, ndl, ndllen);
                }
                /* negative offsets are the documented "from the end" form */
                if (haylen > 0) {
                        nfailed += probe(config, buffer, hay, haylen, -haylen, ndl, ndllen, 1);
                        nfailed += probe(config, buffer, hay, haylen, -1, ndl, ndllen, 1);
                        nfailed += probe(config, buffer, hay, haylen, -(1 + rnd(haylen)), ndl, ndllen, 1);
                }

                medusa_buffer_destroy(buffer);
                if (nfailed > 40) {
                        return nfailed;
                }
        }

        return nfailed;
}

/* hand picked cases, mostly around the ends of the buffer */
static int test_edges (const struct config *config)
{
        static const struct {
                const char *hay;
                const char *ndl;
                int64_t offset;
        } cases[] = {
                { "",       "",     0  },
                { "",       "a",    0  },
                { "a",      "",     0  },
                { "abc",    "abc",  0  },
                { "abc",    "abd",  0  },
                { "abd",    "abc",  0  },
                { "abc",    "ab",   0  },      /* needle is a prefix -> equal  */
                { "ab",     "abc",  0  },      /* haystack runs out  -> less   */
                { "b",      "abc",  0  },      /* runs out but greater         */
                { "a",      "bcd",  0  },
                { "xxabc",  "abc",  2  },
                { "xxab",   "abc",  2  },
                { "xxb",    "abc",  2  },
                { "abc",    "a",    3  },      /* offset at the very end       */
                { "abcabc", "abc",  1  },      /* second occurrence only       */
                { "aaaa",   "aa",   1  },      /* overlapping occurrences      */
                { "AbCaBc", "abc",  0  },      /* case folding                 */
                { "abcd",   "cd",  -2  },      /* negative offset              */
                { "abcd",   "abcd", -4 },
                { "abcd",   "d",   -1  }
        };
        size_t i;
        int nfailed;

        nfailed = 0;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                int64_t wp;
                struct medusa_buffer *buffer;
                const unsigned char *hay = (const unsigned char *) cases[i].hay;
                const unsigned char *ndl = (const unsigned char *) cases[i].ndl;
                int64_t haylen = (int64_t) strlen(cases[i].hay);
                int64_t ndllen = (int64_t) strlen(cases[i].ndl);

                buffer = buffer_create(config);
                if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                        fprintf(stderr, "fail @ %s: can not create buffer\n", config->name);
                        return -1;
                }
                wp = build_content(config, buffer, hay, haylen);
                if (wp < 0) {
                        medusa_buffer_destroy(buffer);
                        return -1;
                }
                nfailed += probe(config, buffer, hay, haylen, cases[i].offset, ndl, ndllen, 1);
                medusa_buffer_destroy(buffer);
        }
        return nfailed;
}

/* binary content, exercised through the mem* entry points and the NUL cases */
static int test_binary (const struct config *config)
{
        int64_t i;
        int64_t wp;
        int nfailed;
        int64_t found;
        unsigned char hay[64];
        struct medusa_buffer *buffer;

        nfailed = 0;
        for (i = 0; i < (int64_t) sizeof(hay); i++) {
                hay[i] = (unsigned char) i;
        }

        buffer = buffer_create(config);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "fail @ %s: can not create buffer\n", config->name);
                return -1;
        }
        wp = build_content(config, buffer, hay, (int64_t) sizeof(hay));
        if (wp < 0) {
                medusa_buffer_destroy(buffer);
                return -1;
        }

        /* every 3 byte window must be found at exactly its own index */
        for (i = 0; i + 3 <= (int64_t) sizeof(hay); i++) {
                found = medusa_buffer_memmem(buffer, 0, hay + i, 3);
                if (found != i) {
                        fprintf(stderr, "fail @ %s/memmem(binary): window: %ld, got: %ld\n",
                                config->name, (long) i, (long) found);
                        nfailed++;
                }
        }
        /* ... and searching from just past it must find nothing */
        for (i = 1; i + 3 <= (int64_t) sizeof(hay); i++) {
                found = medusa_buffer_memmem(buffer, i + 1, hay + i, 3);
                if (found != -ENOENT) {
                        fprintf(stderr, "fail @ %s/memmem(binary,past): window: %ld, got: %ld\n",
                                config->name, (long) i, (long) found);
                        nfailed++;
                }
        }

        /* the content starts with a NUL byte, so strchr must locate it at 0 */
        found = medusa_buffer_strchr(buffer, 0, '\0');
        if (found != 0) {
                fprintf(stderr, "fail @ %s/strchr(NUL): got: %ld, want: 0\n",
                        config->name, (long) found);
                nfailed++;
        }
        found = medusa_buffer_strcasechr(buffer, 0, '\0');
        if (found != 0) {
                fprintf(stderr, "fail @ %s/strcasechr(NUL): got: %ld, want: 0\n",
                        config->name, (long) found);
                nfailed++;
        }
        /* there is only one, so searching past it must report absence */
        found = medusa_buffer_strchr(buffer, 1, '\0');
        if (found != -ENOENT) {
                fprintf(stderr, "fail @ %s/strchr(NUL, offset 1): got: %ld, want: -ENOENT\n",
                        config->name, (long) found);
                nfailed++;
        }
        /* a high byte, to catch any signed char handling */
        found = medusa_buffer_strchr(buffer, 0, (char) 0xff);
        if (found != -ENOENT) {
                fprintf(stderr, "fail @ %s/strchr(0xff): got: %ld, want: -ENOENT\n",
                        config->name, (long) found);
                nfailed++;
        }
        found = medusa_buffer_strchr(buffer, 0, (char) 0x3f);
        if (found != 0x3f) {
                fprintf(stderr, "fail @ %s/strchr(0x3f): got: %ld, want: 63\n",
                        config->name, (long) found);
                nfailed++;
        }

        medusa_buffer_destroy(buffer);
        return nfailed;
}

/* argument validation: nothing may crash, and errors must stay negative */
static int test_invalid (const struct config *config)
{
        int nfailed;
        int64_t wp;
        struct medusa_buffer *buffer;
        const unsigned char hay[] = "abcdef";
        const int64_t haylen = 6;

        nfailed = 0;

        buffer = buffer_create(config);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "fail @ %s: can not create buffer\n", config->name);
                return -1;
        }
        wp = build_content(config, buffer, hay, haylen);
        if (wp < 0) {
                medusa_buffer_destroy(buffer);
                return -1;
        }

#define CHECK_NEG(call, what)                                                           \
        do {                                                                            \
                int64_t _rc = (call);                                                   \
                if (_rc >= 0) {                                                         \
                        fprintf(stderr, "fail @ %s/%s: got: %ld, want: negative\n",     \
                                config->name, what, (long) _rc);                        \
                        nfailed++;                                                      \
                }                                                                       \
        } while (0)

        CHECK_NEG(medusa_buffer_memcmp(NULL, 0, hay, 1),             "memcmp(NULL buffer)");
        CHECK_NEG(medusa_buffer_memcmp(buffer, 0, NULL, 1),          "memcmp(NULL data)");
        CHECK_NEG(medusa_buffer_memcmp(buffer, 0, hay, -1),          "memcmp(negative length)");
        CHECK_NEG(medusa_buffer_memcmp(buffer, haylen + 1, hay, 1),  "memcmp(offset past end)");
        CHECK_NEG(medusa_buffer_memcmp(buffer, -haylen - 1, hay, 1), "memcmp(offset before start)");
        CHECK_NEG(medusa_buffer_strcmp(buffer, 0, NULL),             "strcmp(NULL str)");
        CHECK_NEG(medusa_buffer_strcasecmp(buffer, 0, NULL),         "strcasecmp(NULL str)");
        CHECK_NEG(medusa_buffer_strncmp(buffer, 0, "a", -1),         "strncmp(negative n)");
        CHECK_NEG(medusa_buffer_strncasecmp(buffer, 0, "a", -1),     "strncasecmp(negative n)");
        CHECK_NEG(medusa_buffer_memmem(NULL, 0, hay, 1),             "memmem(NULL buffer)");
        CHECK_NEG(medusa_buffer_memmem(buffer, 0, NULL, 1),          "memmem(NULL data)");
        CHECK_NEG(medusa_buffer_memmem(buffer, 0, hay, -1),          "memmem(negative length)");
        CHECK_NEG(medusa_buffer_memmem(buffer, haylen + 1, hay, 1),  "memmem(offset past end)");
        CHECK_NEG(medusa_buffer_memmem(buffer, -haylen - 1, hay, 1), "memmem(offset before start)");
        CHECK_NEG(medusa_buffer_strstr(buffer, 0, NULL),             "strstr(NULL str)");
        CHECK_NEG(medusa_buffer_strcasestr(buffer, 0, NULL),         "strcasestr(NULL str)");
        CHECK_NEG(medusa_buffer_strstr(NULL, 0, "a"),                "strstr(NULL buffer)");
        CHECK_NEG(medusa_buffer_strchr(NULL, 0, 'a'),                "strchr(NULL buffer)");
        CHECK_NEG(medusa_buffer_strcasechr(NULL, 0, 'a'),            "strcasechr(NULL buffer)");

        /* a needle that cannot fit is absent, never a found index */
        CHECK_NEG(medusa_buffer_memmem(buffer, 0, "abcdefgh", 8),    "memmem(needle longer than buffer)");
        CHECK_NEG(medusa_buffer_strstr(buffer, 0, "abcdefgh"),       "strstr(needle longer than buffer)");
        CHECK_NEG(medusa_buffer_strstr(buffer, haylen, "a"),         "strstr(offset at end)");

#undef CHECK_NEG

        medusa_buffer_destroy(buffer);
        return nfailed;
}

int main (int argc, char *argv[])
{
        size_t c;
        unsigned int nfailed;

        (void) argc;
        (void) argv;

        nfailed = 0;

        for (c = 0; c < sizeof(g_configs) / sizeof(g_configs[0]); c++) {
                const struct config *config = &g_configs[c];
                int rc;

                fprintf(stderr, "config: %s\n", config->name);

                fprintf(stderr, "  test_edges ...\n");
                rc = test_edges(config);
                if (rc != 0) {
                        fprintf(stderr, "  test_edges: fail (%d)\n", rc);
                        nfailed++;
                }

                fprintf(stderr, "  test_binary ...\n");
                rc = test_binary(config);
                if (rc != 0) {
                        fprintf(stderr, "  test_binary: fail (%d)\n", rc);
                        nfailed++;
                }

                fprintf(stderr, "  test_invalid ...\n");
                rc = test_invalid(config);
                if (rc != 0) {
                        fprintf(stderr, "  test_invalid: fail (%d)\n", rc);
                        nfailed++;
                }

                fprintf(stderr, "  test_random ...\n");
                rc = test_random(config, 60);
                if (rc != 0) {
                        fprintf(stderr, "  test_random: fail (%d)\n", rc);
                        nfailed++;
                }
        }

        fprintf(stderr, "probes: %u, of which straddling the ring wraparound: %u\n",
                g_probe_count, g_split_seen);
        if (g_split_seen == 0) {
                fprintf(stderr, "fail (no probe ever straddled the ring wraparound)\n");
                return -1;
        }

        if (nfailed != 0) {
                fprintf(stderr, "fail (%u sub-test(s) failed)\n", nfailed);
                return -1;
        }
        fprintf(stderr, "success\n");
        return 0;
}
