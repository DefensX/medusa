
#include <stdio.h>
#include <unistd.h>

#if defined(__WINDOWS__)
#include <windows.h>
#endif

#include "medusa/error.h"
#include "medusa/timer.h"

int main (int argc, char *argv[])
{
        struct medusa_timer *timer;
        (void) argc;
        (void) argv;
#if defined(__WINDOWS__)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
        timer = medusa_timer_create(NULL, NULL, NULL);
        if (!MEDUSA_IS_ERR_OR_NULL(timer)) {
                fprintf(stderr, "error\n");
                return -1;
        }
        fprintf(stderr, "success\n");
        return 0;
}
