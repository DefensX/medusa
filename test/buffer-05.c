
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "medusa/error.h"
#include "medusa/clock.h"
#include "medusa/iovec.h"
#include "medusa/buffer.h"

static const unsigned int g_types[] = {
        MEDUSA_BUFFER_TYPE_DEFAULT,
        MEDUSA_BUFFER_TYPE_SIMPLE,
        MEDUSA_BUFFER_TYPE_RING
};

static int test_buffer (unsigned int type, unsigned int count)
{
        char *data;

        unsigned int i;
        unsigned int j;

        int rc;
        struct medusa_buffer *buffer;

        int niovecs;
        struct medusa_iovec *iovecs;

        data = malloc(count + 1);
        if (data == NULL) {
                fprintf(stderr, "malloc failed\n");
                return -1;
        }
        memset(data, 0, count + 1);

        buffer = medusa_buffer_create(type);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                fprintf(stderr, "medusa_buffer_create failed\n");
                return -1;
        }

        for (i = 0; i < count; i++) {
                data[i] = 'a' + (rand() % ('z' - 'a'));
        }
        data[count] = '\0';

        rc = medusa_buffer_append(buffer, data, strlen(data));
        if (rc < 0) {
                fprintf(stderr, "medusa_buffer_append failed: %d\n", rc);
                return -1;
        }
        if (rc != (int) strlen(data)) {
                fprintf(stderr, "medusa_buffer_append failed: %d != %d\n", rc, (int) strlen(data));
                return -1;
        }

        for (i = 0; i < count; i++) {
                data[i] = 'a' + (rand() % ('z' - 'a'));
        }
        data[count] = '\0';

        rc = medusa_buffer_printf(buffer, "%s", data);
        if (rc < 0) {
                fprintf(stderr, "medusa_buffer_printf failed: %d\n", rc);
                return -1;
        }
        if (rc != (int) strlen(data)) {
                fprintf(stderr, "medusa_buffer_printf failed: %d != %d\n", rc, (int) strlen(data));
                return -1;
        }

        rc = medusa_buffer_choke(buffer, 0, strlen(data));
        if (rc < 0) {
                fprintf(stderr, "medusa_buffer_choke failed: %d\n", rc);
                return -1;
        }
        if (rc != (int) strlen(data)) {
                fprintf(stderr, "medusa_buffer_choke failed: %d != %d\n", rc, (int) strlen(data));
                return -1;
        }

        niovecs = medusa_buffer_peekv(buffer, 0, -1, NULL, 0);
        if (niovecs < 0) {
                fprintf(stderr, "medusa_buffer_queryv failed\n");
                return -1;
        }

        iovecs = malloc(sizeof(struct medusa_iovec) * niovecs);
        if (iovecs == NULL) {
                fprintf(stderr, "malloc failed\n");
                return -1;
        }

        niovecs = medusa_buffer_peekv(buffer, 0, -1, iovecs, niovecs);
        if (niovecs < 0) {
                fprintf(stderr, "medusa_buffer_queryv failed, count: %d\n", count);
                return -1;
        }

        j = 0;
        for (i = 0; i < (unsigned int) niovecs; i++) {
                if (iovecs[i].iov_len > count - j) {
                        fprintf(stderr, "iovecs[i].iov_len: %d > count - j : %d\n", (int) iovecs[i].iov_len, (int) (count - j));
                        return -1;
                }
                rc = memcmp(data + j, iovecs[i].iov_base, iovecs[i].iov_len);
                if (rc != 0) {
                        fprintf(stderr, "data mismatch @ i: %d / %d, j: %d / %d, iov: %zu\n", i, niovecs, j, count, iovecs[i].iov_len);
                        return -1;
                }
                j += iovecs[i].iov_len;
        }
        if (count != j) {
                fprintf(stderr, "count: %d != j: %d, i: %d / %d\n", count, j, i, niovecs);
                return -1;
        }

        free(iovecs);
        medusa_buffer_destroy(buffer);
        free(data);
        return 0;
}

int main (int argc, char *argv[])
{
        int rc;
        unsigned int i;
        unsigned int j;
        struct timespec timespec_start;
        struct timespec timespec_finish;
        struct timespec timespec_total;
        (void) argc;
        (void) argv;
        fprintf(stderr, "start\n");
        for (i = 0; i < sizeof(g_types) / sizeof(g_types[0]); i++) {
                fprintf(stderr, "type: %d\n", g_types[i]);
                medusa_clock_monotonic(&timespec_start);
                for (j = 0; j < 1000; j++) {
                        rc = test_buffer(g_types[i], j * 16);
                        if (rc != 0) {
                                fprintf(stderr, "fail\n");
                                return -1;
                        }
                }
                medusa_clock_monotonic(&timespec_finish);
                medusa_timespec_sub(&timespec_finish, &timespec_start, &timespec_total);
                fprintf(stderr, "  timespec: %.6f\n", timespec_total.tv_sec + timespec_total.tv_nsec * 1e-9);
        }
        fprintf(stderr, "success\n");
        return 0;
}
