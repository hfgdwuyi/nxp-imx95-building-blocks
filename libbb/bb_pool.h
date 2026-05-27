#ifndef BB_POOL_H
#define BB_POOL_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

// Frame buffer descriptor (DMA-BUF backed for zero-copy hardware pipelines)
typedef struct {
    void        *data;
    size_t       size;
    int          fd;            // dma-buf fd (-1 if malloc-backed)
    atomic_int   refcount;
    uint64_t     pts;
    uint32_t     frame_num;
    int          fourcc;
} bb_frame_t;

// Frame pool with SPSC (lock-free) and MPSC (mutex-protected) operations
typedef struct {
    bb_frame_t   *frames;
    int           count;
    atomic_int    write_idx;
    atomic_int    read_idx;
    pthread_mutex_t mp_mutex;
    pthread_cond_t  mp_cond;
    atomic_int    mp_pending;
} bb_frame_pool_t;

int  bb_pool_init(bb_frame_pool_t *p, int count, size_t frame_size, int use_dmabuf);
void bb_pool_destroy(bb_frame_pool_t *p);

// SPSC (single producer, single consumer) — lock-free
bb_frame_t *bb_pool_sp_acquire(bb_frame_pool_t *p);
void         bb_pool_sp_commit(bb_frame_pool_t *p);
bb_frame_t *bb_pool_sp_dequeue(bb_frame_pool_t *p);
void         bb_pool_sp_release(bb_frame_t *f);

// MPSC (multi producer, single consumer) — mutex-protected
bb_frame_t *bb_pool_mp_acquire(bb_frame_pool_t *p);
void         bb_pool_mp_enqueue(bb_frame_pool_t *p, bb_frame_t *f);
bb_frame_t *bb_pool_mp_dequeue(bb_frame_pool_t *p);
void         bb_pool_mp_release(bb_frame_t *f);

// Refcount for shared frames
void bb_frame_ref(bb_frame_t *f);
void bb_frame_unref(bb_frame_t *f);

#endif
