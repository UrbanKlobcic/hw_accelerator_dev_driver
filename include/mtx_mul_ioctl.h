#ifndef MTX_MUL_IOCTL_H
#define MTX_MUL_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define MTX_MUL_MAX_DIM 4
#define MTX_MUL_MAX_ELEMS (MTX_MUL_MAX_DIM * MTX_MUL_MAX_DIM)
#define MTX_MUL_NUM_BUFS 8

struct mtx_mul_buffer {
    __u32 buf_id;
    __u32 num_elems;                /* how many int32_t entries are valid */
    __s32 data[MTX_MUL_MAX_ELEMS];
};

struct mtx_mul_desc {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t buf_a;
    uint32_t buf_b;
    uint32_t buf_c;
};

struct mtx_mul_buf_status {
    uint32_t buf_id;
    uint32_t allocated;
};

struct mtx_mul_dev_status {
    uint32_t control;
    uint32_t status;
    uint32_t irq_status;
    uint32_t busy;
};

/*
MTX_MUL_IOCTL_MAGIC → distinguishes device family
1 → command number inside that family
struct mtx_mul_job → data type associated with the command
*/

#define MTX_MUL_IOCTL_MAGIC 'q'

#define MTX_MUL_IOCTL_ALLOC_BUF      _IOWR(MTX_MUL_IOCTL_MAGIC, 1, uint32_t)
#define MTX_MUL_IOCTL_FREE_BUF       _IOW(MTX_MUL_IOCTL_MAGIC, 2, uint32_t)
#define MTX_MUL_IOCTL_SUBMIT_DESC    _IOW(MTX_MUL_IOCTL_MAGIC, 3, struct mtx_mul_desc)
#define MTX_MUL_IOCTL_WAIT           _IO(MTX_MUL_IOCTL_MAGIC, 4)
#define MTX_MUL_IOCTL_GET_BUF_STATUS _IOWR(MTX_MUL_IOCTL_MAGIC, 5, struct mtx_mul_buf_status)
#define MTX_MUL_IOCTL_GET_DEV_STATUS _IOR(MTX_MUL_IOCTL_MAGIC, 6, struct mtx_mul_dev_status)

#endif