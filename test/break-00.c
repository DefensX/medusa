
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#include <pthread.h>

#include "medusa/monitor.h"

static void alarm_handler (int sig)
{
        (void) sig;
        abort();
}

struct thread_arg {
        int rc;
        struct medusa_monitor *monitor;
};

static void * thread_worker (void *arg)
{
        struct thread_arg *thread_arg  = (struct thread_arg *) arg;
        usleep(500000);
        fprintf(stderr, "breaking\n");
        thread_arg->rc = medusa_monitor_break(thread_arg->monitor);
        pthread_exit(NULL);
        return NULL;
}

int main (int argc, char *argv[])
{
        pthread_t thread;
        struct medusa_monitor *monitor;

        int rc;
        struct thread_arg thread_arg;

        (void) argc;
        (void) argv;

        srand(time(NULL));
        signal(SIGALRM, alarm_handler);

        alarm(5);

        monitor = medusa_monitor_create_with_options(NULL);
        if (monitor == NULL) {
                fprintf(stderr, "can not create monitor\n");
                return -1;
        }

        thread_arg.rc      = -1;
        thread_arg.monitor = monitor;
        pthread_create(&thread, NULL, thread_worker, &thread_arg);

        fprintf(stderr, "run...\n");
        rc = medusa_monitor_run(monitor);
        if (rc != 0) {
                return -1;
        }
        fprintf(stderr, "done...\n");

        pthread_join(thread, NULL);
        if (thread_arg.rc != 0) {
                fprintf(stderr, "rc: %d is invalid\n", thread_arg.rc);
                return -1;
        }
        medusa_monitor_destroy(monitor);
        fprintf(stderr, "finish...\n");
        return 0;
}
