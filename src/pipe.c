
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "pipe.h"

#if defined(__WINDOWS__)

#include <winsock2.h>
#include <wspiapi.h>

#include <winsock2.h>
#include <mswsock.h>

static int __socketpair (int family, int type, int protocol, int fd[2])
{
	SOCKET a        = INVALID_SOCKET;
	SOCKET b        = INVALID_SOCKET;
	SOCKET listener = INVALID_SOCKET;
	struct sockaddr_in sa;
	struct sockaddr_in sb;
	int size;

	if (fd == NULL || protocol != 0 || family != AF_INET) {
		return -EINVAL;
	}
	if (type != SOCK_STREAM && type != SOCK_DGRAM) {
		return -EINVAL;
	}
	fd[0] = -1;
	fd[1] = -1;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family      = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port        = 0;
	sb = sa;

	if (type == SOCK_STREAM) {
		int nodelay = 1;

		listener = socket(AF_INET, SOCK_STREAM, 0);
		if (listener == INVALID_SOCKET) {
			goto bail;
		}
		if (bind(listener, (struct sockaddr *) &sa, sizeof(sa)) == SOCKET_ERROR) {
			goto bail;
		}
		if (listen(listener, 1) == SOCKET_ERROR) {
			goto bail;
		}
		size = sizeof(sa);
		if (getsockname(listener, (struct sockaddr *) &sa, &size) == SOCKET_ERROR || size != sizeof(sa)) {
			goto bail;
		}

		a = socket(AF_INET, SOCK_STREAM, 0);
		if (a == INVALID_SOCKET) {
			goto bail;
		}
		if (connect(a, (struct sockaddr *) &sa, sizeof(sa)) == SOCKET_ERROR) {
			goto bail;
		}

		size = sizeof(sb);
		b = accept(listener, (struct sockaddr *) &sb, &size);
		if (b == INVALID_SOCKET || size != sizeof(sb)) {
			goto bail;
		}

		/* the accepted peer must be our connector, not another local process that raced us */
		size = sizeof(sa);
		if (getsockname(a, (struct sockaddr *) &sa, &size) == SOCKET_ERROR || size != sizeof(sa)) {
			goto bail;
		}
		if (sa.sin_family      != sb.sin_family ||
		    sa.sin_addr.s_addr != sb.sin_addr.s_addr ||
		    sa.sin_port        != sb.sin_port) {
			goto bail;
		}

		/* a wakeup is a few bytes, nagle would hold it back until the previous one is acked */
		setsockopt(a, IPPROTO_TCP, TCP_NODELAY, (const char *) &nodelay, sizeof(nodelay));
		setsockopt(b, IPPROTO_TCP, TCP_NODELAY, (const char *) &nodelay, sizeof(nodelay));

		closesocket(listener);
		listener = INVALID_SOCKET;
	} else {
		DWORD bytes;
		BOOL behavior = FALSE;
		unsigned long nb;
		char junk[1];
		int bufsize = 1024 * 1024;

		a = socket(AF_INET, SOCK_DGRAM, 0);
		b = socket(AF_INET, SOCK_DGRAM, 0);
		if (a == INVALID_SOCKET || b == INVALID_SOCKET) {
			goto bail;
		}
		if (bind(a, (struct sockaddr *) &sa, sizeof(sa)) == SOCKET_ERROR ||
		    bind(b, (struct sockaddr *) &sb, sizeof(sb)) == SOCKET_ERROR) {
			goto bail;
		}
		size = sizeof(sa);
		if (getsockname(a, (struct sockaddr *) &sa, &size) == SOCKET_ERROR || size != sizeof(sa)) {
			goto bail;
		}
		size = sizeof(sb);
		if (getsockname(b, (struct sockaddr *) &sb, &size) == SOCKET_ERROR || size != sizeof(sb)) {
			goto bail;
		}

		/* each socket now only accepts datagrams from the other one */
		if (connect(a, (struct sockaddr *) &sb, sizeof(sb)) == SOCKET_ERROR ||
		    connect(b, (struct sockaddr *) &sa, sizeof(sa)) == SOCKET_ERROR) {
			goto bail;
		}

		/* otherwise recv() fails with WSAECONNRESET after ICMP unreachable */
		WSAIoctl(a, SIO_UDP_CONNRESET, &behavior, sizeof(behavior), NULL, 0, &bytes, NULL, NULL);
		WSAIoctl(b, SIO_UDP_CONNRESET, &behavior, sizeof(behavior), NULL, 0, &bytes, NULL, NULL);

		setsockopt(a, SOL_SOCKET, SO_RCVBUF, (const char *) &bufsize, sizeof(bufsize));
		setsockopt(b, SOL_SOCKET, SO_RCVBUF, (const char *) &bufsize, sizeof(bufsize));

		/* drop anything a foreign process sent between bind() and connect() */
		nb = 1;
		ioctlsocket(a, FIONBIO, &nb);
		ioctlsocket(b, FIONBIO, &nb);
		while (recv(a, junk, sizeof(junk), 0) != SOCKET_ERROR || WSAGetLastError() == WSAEMSGSIZE) { }
		while (recv(b, junk, sizeof(junk), 0) != SOCKET_ERROR || WSAGetLastError() == WSAEMSGSIZE) { }
		nb = 0;
		ioctlsocket(a, FIONBIO, &nb);
		ioctlsocket(b, FIONBIO, &nb);
	}

	fd[0] = (int) a;
	fd[1] = (int) b;
	return 0;

bail:
	if (listener != INVALID_SOCKET) {
		closesocket(listener);
	}
	if (a != INVALID_SOCKET) {
		closesocket(a);
	}
	if (b != INVALID_SOCKET) {
		closesocket(b);
	}
	return -EIO;
}

__attribute__ ((visibility ("default"))) int medusa_pipe (int pipefd[2])
{
        return __socketpair(AF_INET, SOCK_STREAM, 0, pipefd);
}

__attribute__ ((visibility ("default"))) int medusa_pipe2 (int pipefd[2], unsigned int flags)
{
        int rc;
        unsigned long nonblocking;
        rc = __socketpair(AF_INET, (flags & MEDUSA_PIPE_FLAG_DGRAM) ? SOCK_DGRAM : SOCK_STREAM, 0, pipefd);
        if (rc < 0) {
                return rc;
        }
        nonblocking = (flags & MEDUSA_PIPE_FLAG_NONBLOCK) ? 1 : 0;
        rc = ioctlsocket(pipefd[0], FIONBIO, &nonblocking);
        if (rc != 0) {
                closesocket(pipefd[0]);
                closesocket(pipefd[1]);
                return rc;
        }
        nonblocking = (flags & MEDUSA_PIPE_FLAG_NONBLOCK) ? 1 : 0;
        rc = ioctlsocket(pipefd[1], FIONBIO, &nonblocking);
        if (rc != 0) {
                closesocket(pipefd[0]);
                closesocket(pipefd[1]);
                return rc;
        }
        return 0;
}

#else

__attribute__ ((visibility ("default"))) int medusa_pipe (int pipefd[2])
{
        return pipe(pipefd);
}

#if defined(__DARWIN__)

__attribute__ ((visibility ("default"))) int medusa_pipe2 (int pipefd[2], unsigned int flags)
{
	int ret;
	if (!flags) {
		return pipe(pipefd);
	}
	ret = pipe(pipefd);
	if (ret) {
		return ret;
	}
	if (flags & MEDUSA_PIPE_FLAG_NONBLOCK) {
		fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
		fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
	}
	return 0;
}

#else

__attribute__ ((visibility ("default"))) int medusa_pipe2 (int pipefd[2], unsigned int flags)
{
        unsigned int options;
        options = 0;
        if (flags & MEDUSA_PIPE_FLAG_NONBLOCK) {
                options |= O_NONBLOCK;
        }
        return pipe2(pipefd, options);
}

#endif

#endif
