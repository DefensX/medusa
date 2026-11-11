
/* linked into every test on windows, sockets need winsock started before main */

#if defined(__WINDOWS__)

#include <winsock2.h>

__attribute__ ((constructor)) static void test_windows_init (void)
{
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2, 2), &wsadata);
}

#endif
