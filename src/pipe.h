
#if !defined(MEDUSA_PIPE_H)
#define MEDUSA_PIPE_H

enum {
        MEDUSA_PIPE_FLAG_NONE           = 0x00000000,
        MEDUSA_PIPE_FLAG_NONBLOCK       = 0x00000001
#define MEDUSA_PIPE_FLAG_NONE           MEDUSA_PIPE_FLAG_NONE
#define MEDUSA_PIPE_FLAG_NONBLOCK       MEDUSA_PIPE_FLAG_NONBLOCK
};

/* both return 0, or -1 and set errno. windows has no pipe, errno is ENOTSUP. */
int medusa_pipe (int pipefd[2]);
int medusa_pipe2 (int pipefd[2], unsigned int flags);

#endif
