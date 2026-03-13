#ifndef DMA_MTX_MUL_IOCTL_H
#define DMA_MTX_MUL_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define DMA_MTX_MUL_MAX_DIM 4
#define DMA_MTX_MUL_MAX_ELEMS (DMA_MTX_MUL_MAX_DIM * DMA_MTX_MUL_MAX_DIM)
#define DMA_MTX_MUL_NUM_BUFS 8
#define DMA_MTX_MUL_QUEUE_DEPTH 8

/*
 * Users allocate/free buffers with ioctls as before.
 * Version 3.1 keeps:
 *   - write(fd, &desc, sizeof(desc)) to submit a job
 *   - mmap(fd, ..., offset = DMA_MTX_MUL_MMAP_OFFSET(buf_id)) to map a buffer
 *
 * Version 3.1 changes completion delivery:
 *   - read(fd, &completion, sizeof(completion)) returns one completion record
 *   - DMA_MTX_MUL_IOCTL_DEQUEUE_DONE returns a completion record too
 *
 * Each mapped buffer occupies one page in the virtual address space.
 * Only the first DMA_MTX_MUL_BUF_BYTES bytes are meaningful payload.
 */
#define DMA_MTX_MUL_BUF_BYTES (DMA_MTX_MUL_MAX_ELEMS * sizeof(int32_t))
#define DMA_MTX_MUL_MMAP_STRIDE 4096UL
#define DMA_MTX_MUL_MMAP_OFFSET(buf_id) ((off_t)(buf_id) * (off_t)DMA_MTX_MUL_MMAP_STRIDE)

enum dma_mtx_mul_completion_status {
    DMA_MTX_MUL_STATUS_OK = 0,
    DMA_MTX_MUL_STATUS_CANCELED = 1,
    DMA_MTX_MUL_STATUS_FAILED = 2,
};

struct dma_mtx_mul_buffer {
    uint32_t buf_id;
    uint32_t num_elems;
    int32_t data[DMA_MTX_MUL_MAX_ELEMS];
};

struct dma_mtx_mul_desc {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t buf_a;
    uint32_t buf_b;
    uint32_t buf_c;
};

struct dma_mtx_mul_completion {
    struct dma_mtx_mul_desc desc;
    int32_t status;
    uint32_t output_elems;
    uint32_t reserved;
};

struct dma_mtx_mul_buf_info {
    uint32_t buf_id;
    uint32_t allocated;
    uint32_t num_elems;
    uint32_t refcnt;
    uint64_t dma_addr;
};

struct dma_mtx_mul_dev_status {
    uint32_t control;
    uint32_t status;
    uint32_t irq_status;
    uint32_t busy;
    uint32_t pending_count;
    uint32_t done_count;
    uint32_t active_jobs;
    uint32_t closing;
};

#define DMA_MTX_MUL_IOCTL_MAGIC 'q'

#define DMA_MTX_MUL_IOCTL_ALLOC_BUF      _IOWR(DMA_MTX_MUL_IOCTL_MAGIC, 1, uint32_t)
#define DMA_MTX_MUL_IOCTL_FREE_BUF       _IOW(DMA_MTX_MUL_IOCTL_MAGIC, 2, uint32_t)
#define DMA_MTX_MUL_IOCTL_SUBMIT_DESC    _IOW(DMA_MTX_MUL_IOCTL_MAGIC, 3, struct dma_mtx_mul_desc)
#define DMA_MTX_MUL_IOCTL_WAIT           _IO(DMA_MTX_MUL_IOCTL_MAGIC, 4)
#define DMA_MTX_MUL_IOCTL_GET_BUF_INFO   _IOWR(DMA_MTX_MUL_IOCTL_MAGIC, 5, struct dma_mtx_mul_buf_info)
#define DMA_MTX_MUL_IOCTL_GET_DEV_STATUS _IOR(DMA_MTX_MUL_IOCTL_MAGIC, 6, struct dma_mtx_mul_dev_status)
#define DMA_MTX_MUL_IOCTL_DEQUEUE_DONE   _IOR(DMA_MTX_MUL_IOCTL_MAGIC, 7, struct dma_mtx_mul_completion)
#define DMA_MTX_MUL_IOCTL_WRITE_BUF      _IOW(DMA_MTX_MUL_IOCTL_MAGIC, 8, struct dma_mtx_mul_buffer)
#define DMA_MTX_MUL_IOCTL_READ_BUF       _IOWR(DMA_MTX_MUL_IOCTL_MAGIC, 9, struct dma_mtx_mul_buffer)

#endif