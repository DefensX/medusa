
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "medusa/error.h"
#include "medusa/buffer.h"

static const unsigned int g_types[] = {
        MEDUSA_BUFFER_TYPE_DEFAULT,
        MEDUSA_BUFFER_TYPE_SIMPLE,
};

static int buffer_onevent (struct medusa_buffer *buffer, unsigned int events, void *context, void *param)
{
        unsigned int *bevents = (unsigned int *) context;
        (void) buffer;
        (void) param;
        *bevents |= events;
        return 0;
}

static int test_buffer (unsigned int type)
{
        int rc;
        unsigned int bevents;
        struct medusa_buffer *buffer;
        struct medusa_buffer_init_options buffer_init_options;
        buffer = NULL;
        bevents = 0;
        rc = medusa_buffer_init_options_default(&buffer_init_options);
        if (rc != 0) {
                goto bail;
        }
        buffer_init_options.type    = type;
        buffer_init_options.onevent = buffer_onevent;
        buffer_init_options.context = &bevents;
        buffer = medusa_buffer_create_with_options(&buffer_init_options);
        if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                goto bail;
        }
        rc = medusa_buffer_append(buffer, "data", 4);
        if (rc != 4) {
                goto bail;
        }
        medusa_buffer_destroy(buffer);
        if (bevents != (MEDUSA_BUFFER_EVENT_WRITE |
                        MEDUSA_BUFFER_EVENT_DESTROY)) {
                goto bail;
        }
        return 0;
bail:   if (MEDUSA_IS_ERR_OR_NULL(buffer)) {
                medusa_buffer_destroy(buffer);
        }
        return -1;
}

int main (int argc, char *argv[])
{
        int rc;
        unsigned int i;
        (void) argc;
        (void) argv;
        fprintf(stderr, "start\n");
        for (i = 0; i < sizeof(g_types) / sizeof(g_types[0]); i++) {
                fprintf(stderr, "type: %d\n", g_types[i]);
                rc = test_buffer(g_types[i]);
                if (rc != 0) {
                        fprintf(stderr, "fail\n");
                        return -1;
                }
        }
        fprintf(stderr, "success\n");
        return 0;
}
