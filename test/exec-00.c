
#include <stdio.h>
#include <unistd.h>

#include "medusa/error.h"
#include "medusa/exec.h"

#if !defined(__LINUX__)

int main (int argc, char *argv[])
{
        (void) argc;
        (void) argv;
        fprintf(stderr, "not supported\n");
        return 0;
}

#else

int main (int argc, char *argv[])
{
        struct medusa_exec *exec;
        (void) argc;
        (void) argv;
        exec = medusa_exec_create(NULL, NULL, NULL, NULL);
        if (!MEDUSA_IS_ERR_OR_NULL(exec)) {
                fprintf(stderr, "error\n");
                return -1;
        }
        fprintf(stderr, "success\n");
        return 0;
}

#endif
