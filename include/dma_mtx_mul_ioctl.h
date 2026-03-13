#ifndef DMA_MTX_MUL_IOCTL_H
#define DMA_MTX_MUL_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define DMA_MTX_MUL_MAX_DIM 4
#define DMA_MTX_MUL_MAX_ELEMS (DMA_MTX_MUL_MAX_DIM * DMA_MTX_MUL_MAX_DIM)
#define DMA_MTX_MUL_NUM_BUFS 8
#define DMA_MTX_MUL_QUEUE_DEPTH 8

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

struct dma_mtx_mul_buf_info {
    uint32_t buf_id;
    uint32_t allocated;
    uint32_t num_elems;
    uint64_t dma_addr;
};

struct dma_mtx_mul_dev_status {
    uint32_t control;
    uint32_t status;
    uint32_t irq_status;
    uint32_t busy;
    uint32_t pending_count;
    uint32_t done_count;   /* done_count visible to this opener only */
};

#define DMA_MTX_MUL_IOCTL_MAGIC 'q'

#define DMA_MTX_MUL_IOCTL_ALLOC_BUF      _IOWR(DMA_MTX_MUL_IOCTL_MAGIC, 1, uint32_t)
#define DMA_MTX_MUL_IOCTL_FREE_BUF       _IOW(DMA_MTX_MUL_IOCTL_MAGIC, 2, uint32_t)
#define DMA_MTX_MUL_IOCTL_SUBMIT_DESC    _IOW(DMA_MTX_MUL_IOCTL_MAGIC, 3, struct dma_mtx_mul_desc)
#define DMA_MTX_MUL_IOCTL_WAIT           _IO(DMA_MTX_MUL_IOCTL_MAGIC, 4)
#define DMA_MTX_MUL_IOCTL_GET_BUF_INFO   _IOWR(DMA_MTX_MUL_IOCTL_MAGIC, 5, struct dma_mtx_mul_buf_info)
#define DMA_MTX_MUL_IOCTL_GET_DEV_STATUS _IOR(DMA_MTX_MUL_IOCTL_MAGIC, 6, struct dma_mtx_mul_dev_status)
#define DMA_MTX_MUL_IOCTL_DEQUEUE_DONE   _IOR(DMA_MTX_MUL_IOCTL_MAGIC, 7, struct dma_mtx_mul_desc)

#endif