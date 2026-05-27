#include "bb_thread.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#include <sched.h>
#endif

int bb_thread_spawn(bb_thread_t *t)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (t->stack_size > 0)
        pthread_attr_setstacksize(&attr, t->stack_size);

#ifdef __linux__
    struct sched_param sp = { .sched_priority = t->priority };
    if (t->sched_policy != SCHED_OTHER)
        pthread_attr_setschedpolicy(&attr, t->sched_policy);
    if (t->priority > 0)
        pthread_attr_setschedparam(&attr, &sp);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
#else
    (void)t->sched_policy;
    (void)t->priority;
#endif

    int rc = pthread_create(&t->tid, &attr, t->entry, t->arg);
    pthread_attr_destroy(&attr);

#ifdef __linux__
    if (rc == 0 && t->cpu_affinity >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET((unsigned)t->cpu_affinity, &cpuset);
        pthread_setaffinity_np(t->tid, sizeof(cpuset), &cpuset);
    }
#else
    (void)t->cpu_affinity;
#endif

    if (rc == 0 && t->name) {
#ifdef __linux__
        pthread_setname_np(t->tid, t->name);
#else
        (void)t->name;
#endif
    }

    return rc;
}

int bb_thread_join(bb_thread_t *t)
{
    void *ret;
    return pthread_join(t->tid, &ret);
}

void bb_thread_set_name(const char *name)
{
#ifdef __linux__
    prctl(PR_SET_NAME, name, 0, 0, 0);
#endif
    (void)name;
}

int bb_thread_set_rt(int priority)
{
#ifdef __linux__
    struct sched_param sp = { .sched_priority = priority };
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0)
        return -errno;
    return 0;
#else
    (void)priority;
    return -1;
#endif
}

int bb_thread_pin(int cpu)
{
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET((unsigned)cpu, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0)
        return -errno;
    return 0;
#else
    (void)cpu;
    return -1;
#endif
}
