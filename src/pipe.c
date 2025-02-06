
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

static int __socketpair (int family, int type, int protocol, int fd[2])
{
	int listener;
	int connector;
	int acceptor;
	socklen_t size;
	struct sockaddr_in listen_sockaddr;
	struct sockaddr_in connect_sockaddr;

        if (protocol != 0) {
                return -EINVAL;
        }
        if (family != AF_INET) {
                return -EINVAL;
        }
	if (fd == NULL) {
                return -EINVAL;
	}

        fd[0] = -1;
        fd[1] = -1;

	listener  = -1;
	connector = -1;
	acceptor  = -1;

	listener = socket(AF_INET, type, 0);
	if (listener < 0) {
                return -EIO;
        }
	memset(&listen_sockaddr, 0, sizeof(struct sockaddr_in));
	listen_sockaddr.sin_family      = AF_INET;
	listen_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	listen_sockaddr.sin_port        = 0;
	if (bind(listener, (struct sockaddr *) &listen_sockaddr, sizeof (listen_sockaddr)) == -1) {
		goto bail;
        }
	if (listen(listener, 1) == -1) {
		goto bail;
        }

	connector = socket(AF_INET, type, 0);
	if (connector < 0) {
		goto bail;
        }

	memset(&connect_sockaddr, 0, sizeof(struct sockaddr_in));

	size = sizeof(struct sockaddr_in);
	if (getsockname(listener, (struct sockaddr *) &connect_sockaddr, &size) == -1) {
		goto bail;
        }
	if (size != sizeof (connect_sockaddr)) {
		goto bail;
        }
	if (connect(connector, (struct sockaddr *) &connect_sockaddr, sizeof(struct sockaddr_in)) == -1) {
		goto bail;
        }

	size = sizeof(struct sockaddr_in);
	acceptor = accept(listener, (struct sockaddr *) &listen_sockaddr, &size);
	if (acceptor < 0) {
		goto bail;
        }
	if (size != sizeof(struct sockaddr_in)) {
		goto bail;
        }
	size = sizeof(struct sockaddr_in);
	if (getsockname(connector, (struct sockaddr *) &connect_sockaddr, &size) == -1) {
		goto bail;
        }
	if (size                            != sizeof(struct sockaddr_in) ||
            listen_sockaddr.sin_family      != connect_sockaddr.sin_family ||
            listen_sockaddr.sin_addr.s_addr != connect_sockaddr.sin_addr.s_addr ||
            listen_sockaddr.sin_port        != connect_sockaddr.sin_port) {
		goto bail;
        }
	closesocket(listener);
	fd[0] = connector;
	fd[1] = acceptor;

	return 0;

 bail:  if (listener != -1) {
		closesocket(listener);
        }
	if (connector != -1) {
		closesocket(connector);
        }
	if (acceptor != -1) {
		closesocket(acceptor);
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
        rc = __socketpair(AF_INET, SOCK_STREAM, 0, pipefd);
        if (rc < 0) {
                return rc;
        }
        nonblocking = (flags & MEDUSA_PIPE_FLAG_NONBLOCK) ? 1 : 0;
        rc = ioctlsocket(pipefd[0], FIONBIO, &nonblocking);
        if (rc != 0) {
                close(pipefd[0]);
                close(pipefd[1]);
                return rc;
        }
        nonblocking = (flags & MEDUSA_PIPE_FLAG_NONBLOCK) ? 1 : 0;
        rc = ioctlsocket(pipefd[1], FIONBIO, &nonblocking);
        if (rc != 0) {
                close(pipefd[0]);
                close(pipefd[1]);
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
