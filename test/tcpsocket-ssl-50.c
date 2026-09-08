
#if defined(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) && (MEDUSA_TCPSOCKET_OPENSSL_ENABLE ==1)

#define MEDUSA_TEST_TCPSOCKET_SSL 1
#include "tcpsocket-50.c"

#else

#include <stdio.h>

int main (int argc, char *argv[])
{
        (void) argc;
        (void) argv;
        fprintf(stderr, "medusa tcpsocket openssl support is disabled\n");
        return 0;
}

#endif
