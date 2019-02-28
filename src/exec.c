
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <sys/prctl.h>

#include <pthread.h>

#include "error.h"
#include "pool.h"
#include "queue.h"
#include "timer.h"
#include "timer-private.h"
#include "monitor.h"
#include "monitor-private.h"

#include "subject-struct.h"
#include "exec-struct.h"
#include "exec-private.h"

#include "exec.h"

extern char **environ;

#define MEDUSA_EXEC_ENABLE_MASK           0xff
#define MEDUSA_EXEC_ENABLE_SHIFT          0x18

#define MEDUSA_EXEC_USE_POOL              1
#if defined(MEDUSA_EXEC_USE_POOL) && (MEDUSA_EXEC_USE_POOL == 1)
static struct medusa_pool *g_pool;
#endif

static inline unsigned int exec_get_enabled (const struct medusa_exec *exec)
{
        return (exec->flags >> MEDUSA_EXEC_ENABLE_SHIFT) & MEDUSA_EXEC_ENABLE_MASK;
}

static inline void exec_set_enabled (struct medusa_exec *exec, unsigned int enabled)
{
        exec->flags = (exec->flags & ~(MEDUSA_EXEC_ENABLE_MASK << MEDUSA_EXEC_ENABLE_SHIFT)) |
                      ((enabled & MEDUSA_EXEC_ENABLE_MASK) << MEDUSA_EXEC_ENABLE_SHIFT);
}

static pid_t exec_exec (char * const *args, char * const *environment, int *io)
{
        int i;
        int j;
        int n;
        const char **env;
        pid_t pid;

        n = -1;
        env = NULL;

        if (environment != NULL) {
                n = 0;
                for (i = 0; environ[i] != NULL; i++) {
                        n += 1;
                }
                for (i = 0; environment[i] != NULL; i++) {
                        n += 1;
                }
                env = malloc((n + 1) * sizeof(*env));
                if (env == NULL) {
                        goto bail;
                }
                n = 0;
                for (i = 0; environ[i] != NULL; i++) {
                        env[n++] = environ[i];
                }
                for (i = 0; environment[i] != NULL; i++) {
                        for (j = 0; j < n; j++) {
                                if (strncmp(env[j], environment[i], strcspn(environment[i], "=") + 1) == 0) {
                                        env[j] = environment[i];
                                        break;
                                }
                        }
                        if (j >= n) {
                                env[n++] = environment[i];
                        }
                }
                env[n++] = NULL;
        }

        if (io == NULL) {
                n = open("/dev/null", O_RDWR);
                if (n < 0) {
                        goto bail;
                }
        }

        if ((pid = fork()) > 0) {
                if (env != NULL) {
                        free(env);
                }
                return pid;
        } else if (pid == 0) {
                int rc;
                setpgid(0, 0);
                setvbuf(stdout, NULL, _IONBF, 0);
                setvbuf(stderr, NULL, _IONBF, 0);
                fflush(stdin);
                fflush(stdout);
                fflush(stderr);
                if (io == NULL) {
#if 1
                        dup2(n, STDIN_FILENO);
                        dup2(n, STDOUT_FILENO);
                        dup2(n, STDERR_FILENO);
                        close(n);
#endif
                } else {
#if 1
                        dup2(io[0], STDIN_FILENO);
                        dup2(io[1], STDOUT_FILENO);
                        dup2(io[2], STDERR_FILENO);
                        close(io[0]);
                        close(io[1]);
                        close(io[2]);
#endif
                }
                rc = prctl(PR_SET_PDEATHSIG, SIGKILL);
                if (rc == -1) {
                        perror(0);
                        exit(-1);
                }
                if (getppid() == 1) {
                        exit(-1);
                }
                execvpe(args[0], args, (env != NULL) ? ((char * const *) env) : (environ));
                if (env != NULL) {
                        free(env);
                }
                exit(-1);
        }

bail:   if (io == NULL) {
                close(n);
        }
        if (env != NULL) {
                free(env);
        }
        return -1;
}

static pid_t exec_waitpid (pid_t pid, int *status)
{
        return waitpid(pid, status, WNOHANG);
}

static int exec_kill (pid_t pid, int sig)
{
        return kill((pid < 0) ? pid : -pid, sig);
}

static int exec_timer_onevent (struct medusa_timer *timer, unsigned int events, void *context, ...)
{
        int rc;
        pid_t pid;
        int status;
        struct medusa_exec *exec = (struct medusa_exec *) context;
        if (events & MEDUSA_TIMER_EVENT_DESTROY) {
                return 0;
        }
        if (events & MEDUSA_TIMER_EVENT_TIMEOUT) {
                pid = exec_waitpid(exec->pid, &status);
                if (pid < 0) {
                        return -EIO;
                } else if (pid == 0) {
                        return 0;
                } else {
                        medusa_timer_destroy(timer);
                        exec->pid = -1;
                        exec->timer = NULL;
                        exec->wstatus = status;
                        rc = medusa_exec_onevent(exec, MEDUSA_EXEC_EVENT_STOPPED);
                        if (rc < 0) {
                                return rc;
                        }
                }
        }
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_exec_init_options_default (struct medusa_exec_init_options *options)
{
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        memset(options, 0, sizeof(struct medusa_exec_init_options));
        options->interval = 0.1;
        return 0;
}

__attribute__ ((visibility ("default"))) int medusa_exec_init_unlocked (struct medusa_exec *exec, struct medusa_monitor *monitor, const char *argv[], int (*onevent) (struct medusa_exec *exec, unsigned int events, void *context, ...), void *context)
{
        int rc;
        struct medusa_exec_init_options options;
        rc = medusa_exec_init_options_default(&options);
        if (rc < 0) {
                return rc;
        }
        options.monitor = monitor;
        options.argv = argv;
        options.onevent = onevent;
        options.context = context;
        return medusa_exec_init_with_options_unlocked(exec, &options);
}

__attribute__ ((visibility ("default"))) int medusa_exec_init (struct medusa_exec *exec, struct medusa_monitor *monitor, const char *argv[], int (*onevent) (struct medusa_exec *exec, unsigned int events, void *context, ...), void *context)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(monitor);
        rc = medusa_exec_init_unlocked(exec, monitor, argv, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_exec_init_with_options_unlocked (struct medusa_exec *exec, const struct medusa_exec_init_options *options)
{
        int ret;
        int argc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        if (options->argv == NULL) {
                return -EINVAL;
        }
        for (argc = 0; options->argv[argc] != NULL; argc++) {
                ;
        }
        if (argc < 1) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return -EINVAL;
        }
        memset(exec, 0, sizeof(struct medusa_exec));
        exec->pid = -1;
        exec->interval = options->interval;
        exec->argv = malloc(sizeof(char *) * (argc + 1));
        if (exec->argv == NULL) {
                ret = -ENOMEM;
                goto bail;
        }
        memset(exec->argv, 0, sizeof(char *) * (argc + 1));
        for (argc = 0; options->argv[argc] != NULL; argc++) {
                exec->argv[argc] = strdup(options->argv[argc]);
                if (exec->argv[argc] == NULL) {
                        ret = -ENOMEM;
                        goto bail;
                }
        }
        exec->argv[argc++] = NULL;
        exec->onevent = options->onevent;
        exec->context = options->context;
        exec_set_enabled(exec, !!options->enabled);
        medusa_subject_set_type(&exec->subject, MEDUSA_SUBJECT_TYPE_EXEC);
        exec->subject.monitor = NULL;
        return medusa_monitor_add_unlocked(options->monitor, &exec->subject);
bail:   for (argc = 0; options->argv[argc] != NULL; argc++) {
                free(exec->argv[argc]);
        }
        free(exec->argv);
        return ret;
}

__attribute__ ((visibility ("default"))) int medusa_exec_init_with_options (struct medusa_exec *exec, const struct medusa_exec_init_options *options)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return -EINVAL;
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return -EINVAL;
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_exec_init_with_options_unlocked(exec, options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_exec_uninit_unlocked (struct medusa_exec *exec)
{
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return;
        }
        if (exec->subject.monitor != NULL) {
                medusa_monitor_del_unlocked(&exec->subject);
        } else {
                medusa_exec_onevent_unlocked(exec, MEDUSA_EXEC_EVENT_DESTROY);
        }
}

__attribute__ ((visibility ("default"))) void medusa_exec_uninit (struct medusa_exec *exec)
{
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return;
        }
        medusa_monitor_lock(exec->subject.monitor);
        medusa_exec_uninit_unlocked(exec);
        medusa_monitor_unlock(exec->subject.monitor);
}

__attribute__ ((visibility ("default"))) struct medusa_exec * medusa_exec_create_unlocked (struct medusa_monitor *monitor, const char *argv[], int (*onevent) (struct medusa_exec *exec, unsigned int events, void *context, ...), void *context)
{
        int rc;
        struct medusa_exec_init_options options;
        rc = medusa_exec_init_options_default(&options);
        if (rc < 0) {
                return MEDUSA_ERR_PTR(rc);
        }
        options.monitor = monitor;
        options.argv = argv;
        options.onevent = onevent;
        options.context = context;
        return medusa_exec_create_with_options_unlocked(&options);
}

__attribute__ ((visibility ("default"))) struct medusa_exec * medusa_exec_create (struct medusa_monitor *monitor, const char *argv[], int (*onevent) (struct medusa_exec *exec, unsigned int events, void *context, ...), void *context)
{
        struct medusa_exec *rc;
        if (MEDUSA_IS_ERR_OR_NULL(monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(monitor);
        rc = medusa_exec_create_unlocked(monitor, argv, onevent, context);
        medusa_monitor_unlock(monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) struct medusa_exec * medusa_exec_create_with_options_unlocked (const struct medusa_exec_init_options *options)
{
        int rc;
        struct medusa_exec *exec;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->onevent)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
#if defined(MEDUSA_EXEC_USE_POOL) && (MEDUSA_EXEC_USE_POOL == 1)
        exec = medusa_pool_malloc(g_pool);
#else
        exec = malloc(sizeof(struct medusa_exec));
#endif
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return MEDUSA_ERR_PTR(-ENOMEM);
        }
        memset(exec, 0, sizeof(struct medusa_exec));
        rc = medusa_exec_init_with_options_unlocked(exec, options);
        if (rc < 0) {
                medusa_exec_destroy_unlocked(exec);
                return MEDUSA_ERR_PTR(rc);
        }
        exec->subject.flags |= MEDUSA_SUBJECT_FLAG_ALLOC;
        return exec;
}

__attribute__ ((visibility ("default"))) struct medusa_exec * medusa_exec_create_with_options (const struct medusa_exec_init_options *options)
{
        struct medusa_exec *rc;
        if (MEDUSA_IS_ERR_OR_NULL(options)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        if (MEDUSA_IS_ERR_OR_NULL(options->monitor)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(options->monitor);
        rc = medusa_exec_create_with_options_unlocked(options);
        medusa_monitor_unlock(options->monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) void medusa_exec_destroy_unlocked (struct medusa_exec *exec)
{
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return;
        }
        medusa_exec_uninit_unlocked(exec);
}

__attribute__ ((visibility ("default"))) void medusa_exec_destroy (struct medusa_exec *exec)
{
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return;
        }
        medusa_monitor_lock(exec->subject.monitor);
        medusa_exec_uninit_unlocked(exec);
        medusa_monitor_unlock(exec->subject.monitor);
}

__attribute__ ((visibility ("default"))) int medusa_exec_get_pid_unlocked (const struct medusa_exec *exec)
{
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        return exec->pid;
}

__attribute__ ((visibility ("default"))) int medusa_exec_get_pid (const struct medusa_exec *exec)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        medusa_monitor_lock(exec->subject.monitor);
        rc = medusa_exec_get_pid_unlocked(exec);
        medusa_monitor_unlock(exec->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_exec_get_wstatus_unlocked (const struct medusa_exec *exec)
{
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        return exec->wstatus;
}

__attribute__ ((visibility ("default"))) int medusa_exec_get_wstatus (const struct medusa_exec *exec)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        medusa_monitor_lock(exec->subject.monitor);
        rc = medusa_exec_get_wstatus_unlocked(exec);
        medusa_monitor_unlock(exec->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_exec_set_enabled_unlocked (struct medusa_exec *exec, int enabled)
{
        int rc;
        struct medusa_timer_init_options timer_init_options;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        if (exec_get_enabled(exec) == !!enabled) {
                return 0;
        }
        if (!!enabled) {
                if (exec->pid >= 0) {
                        return -EAGAIN;
                }
                if (!MEDUSA_IS_ERR_OR_NULL(exec->timer)) {
                        return -EIO;
                }
                exec->wstatus = 0;
                medusa_timer_init_options_default(&timer_init_options);
                timer_init_options.interval   = exec->interval;
                timer_init_options.singleshot = 0;
                timer_init_options.enabled    = 1;
                timer_init_options.monitor    = exec->subject.monitor;
                timer_init_options.onevent    = exec_timer_onevent;
                timer_init_options.context    = exec;
                exec->timer = medusa_timer_create_with_options_unlocked(&timer_init_options);
                if (MEDUSA_IS_ERR_OR_NULL(exec->timer)) {
                        return MEDUSA_PTR_ERR(exec->timer);
                }
                exec->pid = exec_exec(exec->argv, NULL, NULL);
                if (exec->pid < 0) {
                        return -EIO;
                }
                rc = medusa_exec_onevent_unlocked(exec, MEDUSA_EXEC_EVENT_STARTED);
                if (rc < 0) {
                        return rc;
                }
        } else {
                if (exec->pid < 0) {
                        return -EALREADY;
                }
                exec_kill(exec->pid, SIGKILL);
        }
        exec_set_enabled(exec, !!enabled);
        return medusa_monitor_mod_unlocked(&exec->subject);
}

__attribute__ ((visibility ("default"))) int medusa_exec_set_enabled (struct medusa_exec *exec, int enabled)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        medusa_monitor_lock(exec->subject.monitor);
        rc = medusa_exec_set_enabled_unlocked(exec, enabled);
        medusa_monitor_unlock(exec->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_exec_get_enabled_unlocked (const struct medusa_exec *exec)
{
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        return exec_get_enabled(exec);
}

__attribute__ ((visibility ("default"))) int medusa_exec_get_enabled (const struct medusa_exec *exec)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        medusa_monitor_lock(exec->subject.monitor);
        rc = medusa_exec_get_enabled_unlocked(exec);
        medusa_monitor_unlock(exec->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_exec_enable (struct medusa_exec *exec)
{
        return medusa_exec_set_enabled(exec, 1);
}

__attribute__ ((visibility ("default"))) int medusa_exec_disable (struct medusa_exec *exec)
{
        return medusa_exec_set_enabled(exec, 0);
}

__attribute__ ((visibility ("default"))) int medusa_exec_start (struct medusa_exec *exec)
{
        return medusa_exec_set_enabled(exec, 1);
}

__attribute__ ((visibility ("default"))) int medusa_exec_stop (struct medusa_exec *exec)
{
        return medusa_exec_set_enabled(exec, 0);
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_exec_get_monitor_unlocked (const struct medusa_exec *exec)
{
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        return exec->subject.monitor;
}

__attribute__ ((visibility ("default"))) struct medusa_monitor * medusa_exec_get_monitor (const struct medusa_exec *exec)
{
        struct medusa_monitor *rc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return MEDUSA_ERR_PTR(-EINVAL);
        }
        medusa_monitor_lock(exec->subject.monitor);
        rc = medusa_exec_get_monitor_unlocked(exec);
        medusa_monitor_unlock(exec->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_exec_onevent_unlocked (struct medusa_exec *exec, unsigned int events)
{
        int rc;
        struct medusa_monitor *monitor;
        rc = 0;
        monitor = exec->subject.monitor;
        if (exec->onevent != NULL) {
                medusa_monitor_unlock(monitor);
                rc = exec->onevent(exec, events, exec->context);
                medusa_monitor_lock(monitor);
        }
        if (events & MEDUSA_EXEC_EVENT_DESTROY) {
                if (exec->pid >= 0) {
                        exec_kill(exec->pid, SIGKILL);
                }
                if (!MEDUSA_IS_ERR_OR_NULL(exec->timer)) {
                        medusa_timer_destroy_unlocked(exec->timer);
                }
                if (exec->argv != NULL) {
                        char **ptr;
                        for (ptr = exec->argv; ptr && *ptr; ptr++) {
                                free(*ptr);
                        }
                        free(exec->argv);
                }
                if (exec->subject.flags & MEDUSA_SUBJECT_FLAG_ALLOC) {
#if defined(MEDUSA_EXEC_USE_POOL) && (MEDUSA_EXEC_USE_POOL == 1)
                        medusa_pool_free(exec);
#else
                        free(exec);
#endif
                } else {
                        memset(exec, 0, sizeof(struct medusa_exec));
                }
        }
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_exec_onevent (struct medusa_exec *exec, unsigned int events)
{
        int rc;
        if (MEDUSA_IS_ERR_OR_NULL(exec)) {
                return -EINVAL;
        }
        medusa_monitor_lock(exec->subject.monitor);
        rc = medusa_exec_onevent_unlocked(exec, events);
        medusa_monitor_unlock(exec->subject.monitor);
        return rc;
}

__attribute__ ((visibility ("default"))) int medusa_exec_is_valid_unlocked (const struct medusa_exec *exec)
{
        if (exec->pid < 0) {
                return 0;
        }
        if (exec->onevent == NULL) {
                return 0;
        }
        if (exec_get_enabled(exec) == 0) {
                return 0;
        }
        return 1;
}

__attribute__ ((constructor)) static void exec_constructor (void)
{
#if defined(MEDUSA_EXEC_USE_POOL) && (MEDUSA_EXEC_USE_POOL == 1)
        g_pool = medusa_pool_create("medusa-exec", sizeof(struct medusa_exec), 0, 0, MEDUSA_POOL_FLAG_DEFAULT | MEDUSA_POOL_FLAG_THREAD_SAFE, NULL, NULL, NULL);
#endif
}

__attribute__ ((destructor)) static void exec_destructor (void)
{
#if defined(MEDUSA_EXEC_USE_POOL) && (MEDUSA_EXEC_USE_POOL == 1)
        if (g_pool != NULL) {
                medusa_pool_destroy(g_pool);
        }
#endif
}
