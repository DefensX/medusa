
#include <unistd.h>

#include "medusa/time.h"
#include "medusa/subject.h"

static int callback (void *context, struct medusa_monitor *monitor, struct medusa_subject *subject, unsigned int events)
{
        (void) context;
        (void) monitor;
        (void) subject;
        (void) events;
        return 0;
}

int main (int argc, char *argv[])
{
        struct medusa_subject *subject;
        (void) argc;
        (void) argv;
        subject = medusa_subject_create_io(-1, callback, NULL);
        if (subject != NULL) {
                return -1;
        }
        return 0;
}
