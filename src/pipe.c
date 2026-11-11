
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "pipe.h"

#if defined(__WINDOWS__)

/*
 * windows can only poll sockets, an anonymous pipe can not be in a poll set.
 * a loopback socket pair would do, but filtering drivers (wfp) stall, slow
 * down and reset loopback connections, so there is no pipe on windows.
 */

__attribute__ ((visibility ("default"))) int medusa_pipe (int pipefd[2])
{
        (void) pipefd;
        errno = ENOTSUP;
        return -1;
}

__attribute__ ((visibility ("default"))) int medusa_pipe2 (int pipefd[2], unsigned int flags)
{
        (void) pipefd;
        (void) flags;
        errno = ENOTSUP;
        return -1;
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
