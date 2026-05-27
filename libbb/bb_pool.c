#include "bb_pool.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

int bb_pool_init(bb_frame_pool_t *p, int count, size_t frame_size, int use_dmabuf)
{
    memset(p, 0, sizeof(*p));

    p->frames = calloc(count, sizeof(bb_frame_t));
    if (!p->frames) return -1;
    p->count = count;

    for (int i = 0; i < count; i++) {
        bb_frame_t *f = &p->frames[i];
        f->size = frame_size;
        f->fd = -1;

        f->data = use_dmabuf
            ? mmap(NULL, frame_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS
#ifdef __linux__
                   | MAP_LOCKED
#endif
                   , -1, 0)
            : malloc(frame_size);

        if (!f->data) {
            bb_pool_destroy(p);
            return -1;
        }
        atomic_init(&f->refcount, 0);
    }

    atomic_init(&p->write_idx, 0);
    atomic_init(&p->read_idx, 0);
    atomic_init(&p->mp_pending, 0);
    pthread_mutex_init(&p->mp_mutex, NULL);
    pthread_cond_init(&p->mp_cond, NULL);

    return 0;
}

void bb_pool_destroy(bb_frame_pool_t *p)
{
    if (!p->frames) return;

    for (int i = 0; i < p->count; i++) {
        bb_frame_t *f = &p->frames[i];
        if (f->data) {
            if (f->fd >= 0) {
                munmap(f->data, f->size);
                close(f->fd);
            } else {
                free(f->data);
            }
        }
    }
    free(p->frames);

    pthread_mutex_destroy(&p->mp_mutex);
    pthread_cond_destroy(&p->mp_cond);
    memset(p, 0, sizeof(*p));
}

/* ---- SPSC (lock-free) ---- */

bb_frame_t *bb_pool_sp_acquire(bb_frame_pool_t *p)
{
    int w = atomic_load(&p->write_idx);
    int r = atomic_load(&p->read_idx);
    if (w - r >= p->count)
        return NULL;

    int idx = w % p->count;
    bb_frame_t *f = &p->frames[idx];
    int expected = 0;
    if (!atomic_compare_exchange_strong(&f->refcount, &expected, 1))
        return NULL;
    return f;
}

void bb_pool_sp_commit(bb_frame_pool_t *p)
{
    atomic_fetch_add(&p->write_idx, 1);
}

bb_frame_t *bb_pool_sp_dequeue(bb_frame_pool_t *p)
{
    int w = atomic_load(&p->write_idx);
    int r = atomic_load(&p->read_idx);
    if (r >= w) return NULL;

    int idx = r % p->count;
    bb_frame_t *f = &p->frames[idx];
    if (atomic_load(&f->refcount) < 1) return NULL;
    atomic_fetch_add(&p->read_idx, 1);
    return f;
}

void bb_pool_sp_release(bb_frame_t *f)
{
    atomic_store(&f->refcount, 0);
}

/* ---- MPSC (mutex-protected) ---- */

bb_frame_t *bb_pool_mp_acquire(bb_frame_pool_t *p)
{
    for (int i = 0; i < p->count; i++) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&p->frames[i].refcount, &expected, 1))
            return &p->frames[i];
    }
    return NULL;
}

void bb_pool_mp_enqueue(bb_frame_pool_t *p, bb_frame_t *f)
{
    atomic_fetch_add(&f->refcount, 1);
    pthread_mutex_lock(&p->mp_mutex);
    atomic_fetch_add(&p->mp_pending, 1);
    pthread_cond_signal(&p->mp_cond);
    pthread_mutex_unlock(&p->mp_mutex);
}

bb_frame_t *bb_pool_mp_dequeue(bb_frame_pool_t *p)
{
    pthread_mutex_lock(&p->mp_mutex);
    while (atomic_load(&p->mp_pending) == 0)
        pthread_cond_wait(&p->mp_cond, &p->mp_mutex);

    for (int i = 0; i < p->count; i++) {
        if (atomic_load(&p->frames[i].refcount) >= 2) {
            atomic_fetch_sub(&p->mp_pending, 1);
            pthread_mutex_unlock(&p->mp_mutex);
            return &p->frames[i];
        }
    }

    atomic_fetch_sub(&p->mp_pending, 1);
    pthread_mutex_unlock(&p->mp_mutex);
    return NULL;
}

void bb_pool_mp_release(bb_frame_t *f)
{
    atomic_store(&f->refcount, 0);
}

void bb_frame_ref(bb_frame_t *f)
{
    atomic_fetch_add(&f->refcount, 1);
}

void bb_frame_unref(bb_frame_t *f)
{
    atomic_fetch_sub(&f->refcount, 1);
}
