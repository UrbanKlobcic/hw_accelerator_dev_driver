#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "../include/dma_mtx_mul_ioctl.h"

#define DMA_MTX_MUL_BUF_BYTES (DMA_MTX_MUL_MAX_ELEMS * sizeof(int32_t))
#define DEVICE_NAME "dma_mtx_mul"

/* Fake control register bits */
#define DMA_MTX_MUL_CTRL_START   0x1

/* Fake status register values */
#define DMA_MTX_MUL_STATUS_IDLE  0x0
#define DMA_MTX_MUL_STATUS_BUSY  0x1
#define DMA_MTX_MUL_STATUS_DONE  0x2

/* Fake IRQ status bits */
#define DMA_MTX_MUL_IRQ_JOB_DONE 0x1

struct dma_mtx_mul_regs {
    u32 control;
    u32 status;
    u32 irq_status;
};

struct dma_mtx_mul_buf {
    bool allocated;
    void *cpu_addr;
    dma_addr_t dma_handle;
    u32 num_elems;
    u32 refcnt;
};

struct dma_mtx_mul_file_ctx;

struct dma_mtx_mul_job {
    struct dma_mtx_mul_desc desc;
    struct dma_mtx_mul_file_ctx *owner;
};

struct dma_mtx_mul_job_queue {
    struct dma_mtx_mul_job entries[DMA_MTX_MUL_QUEUE_DEPTH];
    u32 head;
    u32 tail;
    u32 count;
};

struct dma_mtx_mul_desc_queue {
    struct dma_mtx_mul_desc entries[DMA_MTX_MUL_QUEUE_DEPTH];
    u32 head;
    u32 tail;
    u32 count;
};

struct dma_mtx_mul_file_ctx {
    pid_t pid;
    bool owned_bufs[DMA_MTX_MUL_NUM_BUFS];
    struct dma_mtx_mul_desc_queue done_q;
    wait_queue_head_t done_wq;
};

struct dma_mtx_mul_device {
    struct dma_mtx_mul_regs regs;
    struct dma_mtx_mul_buf bufs[DMA_MTX_MUL_NUM_BUFS];

    struct dma_mtx_mul_job_queue pending_q;

    bool worker_busy;
    bool stop_worker;

    struct mutex lock;
    wait_queue_head_t wq;
    struct task_struct *worker_thread;

    struct device *devnode;
};

static dev_t dma_mtx_mul_devno;
static struct cdev dma_mtx_mul_cdev;
static struct class *dma_mtx_mul_class;
static struct dma_mtx_mul_device g_dma_mtx_mul_dev;

/* ---------------- queue helpers ---------------- */

static bool dma_mtx_mul_job_q_is_full(const struct dma_mtx_mul_job_queue *q)
{
    return q->count == DMA_MTX_MUL_QUEUE_DEPTH;
}

static bool dma_mtx_mul_job_q_is_empty(const struct dma_mtx_mul_job_queue *q)
{
    return q->count == 0;
}

static int dma_mtx_mul_job_q_push(struct dma_mtx_mul_job_queue *q,
                                  const struct dma_mtx_mul_job *job)
{
    if (dma_mtx_mul_job_q_is_full(q))
        return -ENOSPC;

    q->entries[q->tail] = *job;
    q->tail = (q->tail + 1) % DMA_MTX_MUL_QUEUE_DEPTH;
    q->count++;
    return 0;
}

static int dma_mtx_mul_job_q_pop(struct dma_mtx_mul_job_queue *q,
                                 struct dma_mtx_mul_job *job)
{
    if (dma_mtx_mul_job_q_is_empty(q))
        return -ENOENT;

    *job = q->entries[q->head];
    q->head = (q->head + 1) % DMA_MTX_MUL_QUEUE_DEPTH;
    q->count--;
    return 0;
}

static bool dma_mtx_mul_desc_q_is_full(const struct dma_mtx_mul_desc_queue *q)
{
    return q->count == DMA_MTX_MUL_QUEUE_DEPTH;
}

static bool dma_mtx_mul_desc_q_is_empty(const struct dma_mtx_mul_desc_queue *q)
{
    return q->count == 0;
}

static int dma_mtx_mul_desc_q_push(struct dma_mtx_mul_desc_queue *q,
                                   const struct dma_mtx_mul_desc *desc)
{
    if (dma_mtx_mul_desc_q_is_full(q))
        return -ENOSPC;

    q->entries[q->tail] = *desc;
    q->tail = (q->tail + 1) % DMA_MTX_MUL_QUEUE_DEPTH;
    q->count++;
    return 0;
}

static int dma_mtx_mul_desc_q_pop(struct dma_mtx_mul_desc_queue *q,
                                  struct dma_mtx_mul_desc *desc)
{
    if (dma_mtx_mul_desc_q_is_empty(q))
        return -ENOENT;

    *desc = q->entries[q->head];
    q->head = (q->head + 1) % DMA_MTX_MUL_QUEUE_DEPTH;
    q->count--;
    return 0;
}

/* ---------------- helpers ---------------- */

static void dma_mtx_mul_reset_regs(struct dma_mtx_mul_device *dev)
{
    dev->regs.control = 0;
    dev->regs.status = DMA_MTX_MUL_STATUS_IDLE;
    dev->regs.irq_status = 0;
}

static bool dma_mtx_mul_buf_valid(u32 buf_id)
{
    return buf_id < DMA_MTX_MUL_NUM_BUFS;
}

static int dma_mtx_mul_validate_desc(const struct dma_mtx_mul_desc *d)
{
    if (d->m == 0 || d->n == 0 || d->k == 0)
        return -EINVAL;

    if (d->m > DMA_MTX_MUL_MAX_DIM ||
        d->n > DMA_MTX_MUL_MAX_DIM ||
        d->k > DMA_MTX_MUL_MAX_DIM)
        return -EINVAL;

    if (!dma_mtx_mul_buf_valid(d->buf_a) ||
        !dma_mtx_mul_buf_valid(d->buf_b) ||
        !dma_mtx_mul_buf_valid(d->buf_c))
        return -EINVAL;

    return 0;
}

/* ---------------- open/release ---------------- */

static int dma_mtx_mul_open(struct inode *inode, struct file *file)
{
    struct dma_mtx_mul_file_ctx *ctx;

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    ctx->pid = task_pid_nr(current);
    init_waitqueue_head(&ctx->done_wq);
    file->private_data = ctx;

    pr_info("dma_mtx_mul: device opened by pid=%d\n", ctx->pid);
    return 0;
}

static int dma_mtx_mul_release(struct inode *inode, struct file *file)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    int i;

    if (!ctx)
        return 0;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    for (i = 0; i < DMA_MTX_MUL_NUM_BUFS; i++) {
        if (ctx->owned_bufs[i] && g_dma_mtx_mul_dev.bufs[i].allocated) {
            void *cpu_addr;
            dma_addr_t dma_handle;

            if (g_dma_mtx_mul_dev.bufs[i].refcnt != 0) {
                pr_warn("dma_mtx_mul: releasing pid=%d while buffer %d still referenced\n",
                        ctx->pid, i);
                continue;
            }

            cpu_addr = g_dma_mtx_mul_dev.bufs[i].cpu_addr;
            dma_handle = g_dma_mtx_mul_dev.bufs[i].dma_handle;

            g_dma_mtx_mul_dev.bufs[i].allocated = false;
            g_dma_mtx_mul_dev.bufs[i].cpu_addr = NULL;
            g_dma_mtx_mul_dev.bufs[i].dma_handle = (dma_addr_t)0;
            g_dma_mtx_mul_dev.bufs[i].num_elems = 0;

            mutex_unlock(&g_dma_mtx_mul_dev.lock);

            dma_free_coherent(g_dma_mtx_mul_dev.devnode,
                              DMA_MTX_MUL_BUF_BYTES,
                              cpu_addr,
                              dma_handle);

            mutex_lock(&g_dma_mtx_mul_dev.lock);
        }
    }

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    pr_info("dma_mtx_mul: device closed by pid=%d\n", ctx->pid);
    kfree(ctx);
    file->private_data = NULL;
    return 0;
}

/* ---------------- read/write ---------------- */

static ssize_t dma_mtx_mul_write(struct file *file,
                                 const char __user *buf,
                                 size_t count,
                                 loff_t *ppos)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    struct dma_mtx_mul_buffer ubuf;

    if (count != sizeof(ubuf))
        return -EINVAL;
    if (*ppos != 0)
        return -ESPIPE;

    if (copy_from_user(&ubuf, buf, sizeof(ubuf)))
        return -EFAULT;

    if (!dma_mtx_mul_buf_valid(ubuf.buf_id))
        return -EINVAL;
    if (ubuf.num_elems > DMA_MTX_MUL_MAX_ELEMS)
        return -EINVAL;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    if (!g_dma_mtx_mul_dev.bufs[ubuf.buf_id].allocated ||
        !ctx->owned_bufs[ubuf.buf_id]) {
        mutex_unlock(&g_dma_mtx_mul_dev.lock);
        return -EPERM;
    }

    memcpy(g_dma_mtx_mul_dev.bufs[ubuf.buf_id].cpu_addr,
           ubuf.data,
           sizeof(ubuf.data));
    g_dma_mtx_mul_dev.bufs[ubuf.buf_id].num_elems = ubuf.num_elems;

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    return sizeof(ubuf);
}

static ssize_t dma_mtx_mul_read(struct file *file,
                                char __user *buf,
                                size_t count,
                                loff_t *ppos)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    struct dma_mtx_mul_buffer ubuf;

    if (count != sizeof(ubuf))
        return -EINVAL;
    if (*ppos != 0)
        return -ESPIPE;

    if (copy_from_user(&ubuf, buf, sizeof(ubuf)))
        return -EFAULT;

    if (!dma_mtx_mul_buf_valid(ubuf.buf_id))
        return -EINVAL;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    if (!g_dma_mtx_mul_dev.bufs[ubuf.buf_id].allocated ||
        !ctx->owned_bufs[ubuf.buf_id]) {
        mutex_unlock(&g_dma_mtx_mul_dev.lock);
        return -EPERM;
    }

    ubuf.num_elems = g_dma_mtx_mul_dev.bufs[ubuf.buf_id].num_elems;
    memcpy(ubuf.data,
           g_dma_mtx_mul_dev.bufs[ubuf.buf_id].cpu_addr,
           sizeof(ubuf.data));

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    if (copy_to_user(buf, &ubuf, sizeof(ubuf)))
        return -EFAULT;

    return sizeof(ubuf);
}

/* ---------------- worker ---------------- */

static int dma_mtx_mul_worker_fn(void *data)
{
    struct dma_mtx_mul_device *dev = data;

    pr_info("dma_mtx_mul: worker thread started\n");

    while (!kthread_should_stop()) {
        struct dma_mtx_mul_job job;
        struct dma_mtx_mul_desc d;
        struct dma_mtx_mul_file_ctx *owner;
        int32_t *a;
        int32_t *b;
        int32_t *c;
        u32 i, j, p;

        wait_event_interruptible(dev->wq,
                                 !dma_mtx_mul_job_q_is_empty(&dev->pending_q) ||
                                 dev->stop_worker ||
                                 kthread_should_stop());

        if (dev->stop_worker || kthread_should_stop())
            break;

        mutex_lock(&dev->lock);

        if (dma_mtx_mul_job_q_pop(&dev->pending_q, &job)) {
            mutex_unlock(&dev->lock);
            continue;
        }

        d = job.desc;
        owner = job.owner;

        dev->worker_busy = true;
        dev->regs.control |= DMA_MTX_MUL_CTRL_START;
        dev->regs.status = DMA_MTX_MUL_STATUS_BUSY;
        dev->regs.irq_status = 0;

        if (!dev->bufs[d.buf_a].allocated ||
            !dev->bufs[d.buf_b].allocated ||
            !dev->bufs[d.buf_c].allocated) {
            if (dev->bufs[d.buf_a].refcnt > 0)
                dev->bufs[d.buf_a].refcnt--;
            if (dev->bufs[d.buf_b].refcnt > 0)
                dev->bufs[d.buf_b].refcnt--;
            if (dev->bufs[d.buf_c].refcnt > 0)
                dev->bufs[d.buf_c].refcnt--;

            dev->worker_busy = false;
            dma_mtx_mul_reset_regs(dev);
            mutex_unlock(&dev->lock);
            wake_up_interruptible(&owner->done_wq);
            wake_up_interruptible(&dev->wq);
            continue;
        }

        a = dev->bufs[d.buf_a].cpu_addr;
        b = dev->bufs[d.buf_b].cpu_addr;
        c = dev->bufs[d.buf_c].cpu_addr;

        mutex_unlock(&dev->lock);

        msleep(10);

        mutex_lock(&dev->lock);

        memset(c, 0, DMA_MTX_MUL_BUF_BYTES);

        for (i = 0; i < d.m; i++) {
            for (j = 0; j < d.n; j++) {
                int32_t sum = 0;

                for (p = 0; p < d.k; p++)
                    sum += a[i * d.k + p] * b[p * d.n + j];

                c[i * d.n + j] = sum;
            }
        }

        dev->bufs[d.buf_c].num_elems = d.m * d.n;

        if (dev->bufs[d.buf_a].refcnt > 0)
            dev->bufs[d.buf_a].refcnt--;
        if (dev->bufs[d.buf_b].refcnt > 0)
            dev->bufs[d.buf_b].refcnt--;
        if (dev->bufs[d.buf_c].refcnt > 0)
            dev->bufs[d.buf_c].refcnt--;

        dma_mtx_mul_desc_q_push(&owner->done_q, &d);

        dev->worker_busy = false;
        dev->regs.status = DMA_MTX_MUL_STATUS_DONE;
        dev->regs.irq_status = DMA_MTX_MUL_IRQ_JOB_DONE;
        dev->regs.control &= ~DMA_MTX_MUL_CTRL_START;

        mutex_unlock(&dev->lock);

        wake_up_interruptible(&owner->done_wq);
        wake_up_interruptible(&dev->wq);
    }

    pr_info("dma_mtx_mul: worker thread stopping\n");
    return 0;
}

/* ---------------- ioctl helpers ---------------- */

static long dma_mtx_mul_handle_alloc_buf(struct file *file, unsigned long arg)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    u32 buf_id;
    int i;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    for (i = 0; i < DMA_MTX_MUL_NUM_BUFS; i++) {
        void *cpu_addr;
        dma_addr_t dma_handle;

        if (g_dma_mtx_mul_dev.bufs[i].allocated)
            continue;

        cpu_addr = dma_alloc_coherent(g_dma_mtx_mul_dev.devnode,
                                      DMA_MTX_MUL_BUF_BYTES,
                                      &dma_handle,
                                      GFP_KERNEL);
        if (!cpu_addr) {
            mutex_unlock(&g_dma_mtx_mul_dev.lock);
            return -ENOMEM;
        }

        memset(cpu_addr, 0, DMA_MTX_MUL_BUF_BYTES);

        g_dma_mtx_mul_dev.bufs[i].allocated = true;
        g_dma_mtx_mul_dev.bufs[i].cpu_addr = cpu_addr;
        g_dma_mtx_mul_dev.bufs[i].dma_handle = dma_handle;
        g_dma_mtx_mul_dev.bufs[i].num_elems = 0;
        g_dma_mtx_mul_dev.bufs[i].refcnt = 0;

        ctx->owned_bufs[i] = true;
        buf_id = i;

        mutex_unlock(&g_dma_mtx_mul_dev.lock);

        if (copy_to_user((void __user *)arg, &buf_id, sizeof(buf_id)))
            return -EFAULT;

        return 0;
    }

    mutex_unlock(&g_dma_mtx_mul_dev.lock);
    return -ENOSPC;
}

static long dma_mtx_mul_handle_free_buf(struct file *file, unsigned long arg)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    u32 buf_id;
    void *cpu_addr;
    dma_addr_t dma_handle;

    if (copy_from_user(&buf_id, (void __user *)arg, sizeof(buf_id)))
        return -EFAULT;

    if (!dma_mtx_mul_buf_valid(buf_id))
        return -EINVAL;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    if (!g_dma_mtx_mul_dev.bufs[buf_id].allocated ||
        !ctx->owned_bufs[buf_id]) {
        mutex_unlock(&g_dma_mtx_mul_dev.lock);
        return -EPERM;
    }

    if (g_dma_mtx_mul_dev.bufs[buf_id].refcnt != 0) {
        mutex_unlock(&g_dma_mtx_mul_dev.lock);
        return -EBUSY;
    }

    cpu_addr = g_dma_mtx_mul_dev.bufs[buf_id].cpu_addr;
    dma_handle = g_dma_mtx_mul_dev.bufs[buf_id].dma_handle;

    g_dma_mtx_mul_dev.bufs[buf_id].allocated = false;
    g_dma_mtx_mul_dev.bufs[buf_id].cpu_addr = NULL;
    g_dma_mtx_mul_dev.bufs[buf_id].dma_handle = (dma_addr_t)0;
    g_dma_mtx_mul_dev.bufs[buf_id].num_elems = 0;
    g_dma_mtx_mul_dev.bufs[buf_id].refcnt = 0;
    ctx->owned_bufs[buf_id] = false;

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    dma_free_coherent(g_dma_mtx_mul_dev.devnode,
                      DMA_MTX_MUL_BUF_BYTES,
                      cpu_addr,
                      dma_handle);

    return 0;
}

static long dma_mtx_mul_handle_submit_desc(struct file *file, unsigned long arg)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    struct dma_mtx_mul_desc d;
    struct dma_mtx_mul_job job;
    int ret;

    if (copy_from_user(&d, (void __user *)arg, sizeof(d)))
        return -EFAULT;

    ret = dma_mtx_mul_validate_desc(&d);
    if (ret)
        return ret;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    if (!g_dma_mtx_mul_dev.bufs[d.buf_a].allocated ||
        !g_dma_mtx_mul_dev.bufs[d.buf_b].allocated ||
        !g_dma_mtx_mul_dev.bufs[d.buf_c].allocated ||
        !ctx->owned_bufs[d.buf_a] ||
        !ctx->owned_bufs[d.buf_b] ||
        !ctx->owned_bufs[d.buf_c]) {
        mutex_unlock(&g_dma_mtx_mul_dev.lock);
        return -EPERM;
    }

    job.desc = d;
    job.owner = ctx;

    ret = dma_mtx_mul_job_q_push(&g_dma_mtx_mul_dev.pending_q, &job);
    if (ret) {
        mutex_unlock(&g_dma_mtx_mul_dev.lock);
        return ret;
    }

    g_dma_mtx_mul_dev.bufs[d.buf_a].refcnt++;
    g_dma_mtx_mul_dev.bufs[d.buf_b].refcnt++;
    g_dma_mtx_mul_dev.bufs[d.buf_c].refcnt++;

    if (!g_dma_mtx_mul_dev.worker_busy) {
        g_dma_mtx_mul_dev.regs.control |= DMA_MTX_MUL_CTRL_START;
        g_dma_mtx_mul_dev.regs.status = DMA_MTX_MUL_STATUS_BUSY;
        g_dma_mtx_mul_dev.regs.irq_status = 0;
    }

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    wake_up_interruptible(&g_dma_mtx_mul_dev.wq);
    return 0;
}

static long dma_mtx_mul_handle_wait(struct file *file)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;

    return wait_event_interruptible(
        ctx->done_wq,
        ({
            bool done;
            mutex_lock(&g_dma_mtx_mul_dev.lock);
            done = !dma_mtx_mul_desc_q_is_empty(&ctx->done_q);
            mutex_unlock(&g_dma_mtx_mul_dev.lock);
            done;
        })
    );
}

static long dma_mtx_mul_handle_dequeue_done(struct file *file, unsigned long arg)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    struct dma_mtx_mul_desc d;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    if (dma_mtx_mul_desc_q_pop(&ctx->done_q, &d)) {
        mutex_unlock(&g_dma_mtx_mul_dev.lock);
        return -EAGAIN;
    }

    if (!g_dma_mtx_mul_dev.worker_busy &&
        dma_mtx_mul_job_q_is_empty(&g_dma_mtx_mul_dev.pending_q))
        dma_mtx_mul_reset_regs(&g_dma_mtx_mul_dev);

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    if (copy_to_user((void __user *)arg, &d, sizeof(d)))
        return -EFAULT;

    return 0;
}

static long dma_mtx_mul_handle_get_buf_info(struct file *file, unsigned long arg)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    struct dma_mtx_mul_buf_info info;

    if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
        return -EFAULT;

    if (!dma_mtx_mul_buf_valid(info.buf_id))
        return -EINVAL;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    if (!ctx->owned_bufs[info.buf_id]) {
        mutex_unlock(&g_dma_mtx_mul_dev.lock);
        return -EPERM;
    }

    info.allocated = g_dma_mtx_mul_dev.bufs[info.buf_id].allocated ? 1 : 0;
    info.num_elems = g_dma_mtx_mul_dev.bufs[info.buf_id].num_elems;
    info.dma_addr = g_dma_mtx_mul_dev.bufs[info.buf_id].allocated ?
                    (uint64_t)g_dma_mtx_mul_dev.bufs[info.buf_id].dma_handle : 0;

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    if (copy_to_user((void __user *)arg, &info, sizeof(info)))
        return -EFAULT;

    return 0;
}

static long dma_mtx_mul_handle_get_dev_status(struct file *file, unsigned long arg)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    struct dma_mtx_mul_dev_status st;

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    st.control = g_dma_mtx_mul_dev.regs.control;
    st.status = g_dma_mtx_mul_dev.regs.status;
    st.irq_status = g_dma_mtx_mul_dev.regs.irq_status;
    st.busy = g_dma_mtx_mul_dev.worker_busy ? 1 : 0;
    st.pending_count = g_dma_mtx_mul_dev.pending_q.count;
    st.done_count = ctx->done_q.count;

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    if (copy_to_user((void __user *)arg, &st, sizeof(st)))
        return -EFAULT;

    return 0;
}

/* ---------------- poll/ioctl ---------------- */

static __poll_t dma_mtx_mul_poll(struct file *file, poll_table *wait)
{
    struct dma_mtx_mul_file_ctx *ctx = file->private_data;
    __poll_t mask = 0;
    bool has_free = false;
    int i;

    poll_wait(file, &ctx->done_wq, wait);
    poll_wait(file, &g_dma_mtx_mul_dev.wq, wait);

    mutex_lock(&g_dma_mtx_mul_dev.lock);

    for (i = 0; i < DMA_MTX_MUL_NUM_BUFS; i++) {
        if (!g_dma_mtx_mul_dev.bufs[i].allocated) {
            has_free = true;
            break;
        }
    }

    if (!dma_mtx_mul_desc_q_is_empty(&ctx->done_q))
        mask |= POLLIN | POLLRDNORM;

    if (has_free || !dma_mtx_mul_job_q_is_full(&g_dma_mtx_mul_dev.pending_q))
        mask |= POLLOUT | POLLWRNORM;

    mutex_unlock(&g_dma_mtx_mul_dev.lock);

    return mask;
}

static long dma_mtx_mul_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case DMA_MTX_MUL_IOCTL_ALLOC_BUF:
        return dma_mtx_mul_handle_alloc_buf(file, arg);
    case DMA_MTX_MUL_IOCTL_FREE_BUF:
        return dma_mtx_mul_handle_free_buf(file, arg);
    case DMA_MTX_MUL_IOCTL_SUBMIT_DESC:
        return dma_mtx_mul_handle_submit_desc(file, arg);
    case DMA_MTX_MUL_IOCTL_WAIT:
        return dma_mtx_mul_handle_wait(file);
    case DMA_MTX_MUL_IOCTL_GET_BUF_INFO:
        return dma_mtx_mul_handle_get_buf_info(file, arg);
    case DMA_MTX_MUL_IOCTL_GET_DEV_STATUS:
        return dma_mtx_mul_handle_get_dev_status(file, arg);
    case DMA_MTX_MUL_IOCTL_DEQUEUE_DONE:
        return dma_mtx_mul_handle_dequeue_done(file, arg);
    default:
        return -ENOTTY;
    }
}

static const struct file_operations dma_mtx_mul_fops = {
    .owner = THIS_MODULE,
    .open = dma_mtx_mul_open,
    .release = dma_mtx_mul_release,
    .read = dma_mtx_mul_read,
    .write = dma_mtx_mul_write,
    .unlocked_ioctl = dma_mtx_mul_ioctl,
    .poll = dma_mtx_mul_poll,
};

/* ---------------- init/exit ---------------- */

static int __init dma_mtx_mul_init(void)
{
    int ret;
    int i;

    pr_info("dma_mtx_mul: initializing dma matrix multiplier driver\n");

    memset(&g_dma_mtx_mul_dev, 0, sizeof(g_dma_mtx_mul_dev));
    mutex_init(&g_dma_mtx_mul_dev.lock);
    init_waitqueue_head(&g_dma_mtx_mul_dev.wq);
    dma_mtx_mul_reset_regs(&g_dma_mtx_mul_dev);

    ret = alloc_chrdev_region(&dma_mtx_mul_devno, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&dma_mtx_mul_cdev, &dma_mtx_mul_fops);
    dma_mtx_mul_cdev.owner = THIS_MODULE;

    ret = cdev_add(&dma_mtx_mul_cdev, dma_mtx_mul_devno, 1);
    if (ret < 0) {
        unregister_chrdev_region(dma_mtx_mul_devno, 1);
        return ret;
    }

    dma_mtx_mul_class = class_create(DEVICE_NAME);
    if (IS_ERR(dma_mtx_mul_class)) {
        cdev_del(&dma_mtx_mul_cdev);
        unregister_chrdev_region(dma_mtx_mul_devno, 1);
        return PTR_ERR(dma_mtx_mul_class);
    }

    g_dma_mtx_mul_dev.devnode = device_create(dma_mtx_mul_class, NULL,
                                              dma_mtx_mul_devno, NULL,
                                              DEVICE_NAME);
    if (IS_ERR(g_dma_mtx_mul_dev.devnode)) {
        class_destroy(dma_mtx_mul_class);
        cdev_del(&dma_mtx_mul_cdev);
        unregister_chrdev_region(dma_mtx_mul_devno, 1);
        return PTR_ERR(g_dma_mtx_mul_dev.devnode);
    }

    g_dma_mtx_mul_dev.devnode->dma_mask =
        &g_dma_mtx_mul_dev.devnode->coherent_dma_mask;
    g_dma_mtx_mul_dev.devnode->coherent_dma_mask = DMA_BIT_MASK(32);

    ret = dma_set_mask_and_coherent(g_dma_mtx_mul_dev.devnode, DMA_BIT_MASK(32));
    if (ret) {
        pr_err("dma_mtx_mul: could not set 32-bit DMA mask\n");
        device_destroy(dma_mtx_mul_class, dma_mtx_mul_devno);
        class_destroy(dma_mtx_mul_class);
        cdev_del(&dma_mtx_mul_cdev);
        unregister_chrdev_region(dma_mtx_mul_devno, 1);
        return ret;
    }

    for (i = 0; i < DMA_MTX_MUL_NUM_BUFS; i++) {
        g_dma_mtx_mul_dev.bufs[i].allocated = false;
        g_dma_mtx_mul_dev.bufs[i].cpu_addr = NULL;
        g_dma_mtx_mul_dev.bufs[i].dma_handle = (dma_addr_t)0;
        g_dma_mtx_mul_dev.bufs[i].num_elems = 0;
        g_dma_mtx_mul_dev.bufs[i].refcnt = 0;
    }

    g_dma_mtx_mul_dev.worker_thread = kthread_run(dma_mtx_mul_worker_fn,
                                                  &g_dma_mtx_mul_dev,
                                                  "dma_mtx_mul_worker");
    if (IS_ERR(g_dma_mtx_mul_dev.worker_thread)) {
        ret = PTR_ERR(g_dma_mtx_mul_dev.worker_thread);
        device_destroy(dma_mtx_mul_class, dma_mtx_mul_devno);
        class_destroy(dma_mtx_mul_class);
        cdev_del(&dma_mtx_mul_cdev);
        unregister_chrdev_region(dma_mtx_mul_devno, 1);
        return ret;
    }

    pr_info("dma_mtx_mul: driver loaded successfully\n");
    pr_info("dma_mtx_mul: major=%d minor=%d\n",
            MAJOR(dma_mtx_mul_devno), MINOR(dma_mtx_mul_devno));

    return 0;
}

static void __exit dma_mtx_mul_exit(void)
{
    int i;

    g_dma_mtx_mul_dev.stop_worker = true;
    wake_up_interruptible(&g_dma_mtx_mul_dev.wq);

    if (g_dma_mtx_mul_dev.worker_thread)
        kthread_stop(g_dma_mtx_mul_dev.worker_thread);

    for (i = 0; i < DMA_MTX_MUL_NUM_BUFS; i++) {
        if (g_dma_mtx_mul_dev.bufs[i].allocated) {
            dma_free_coherent(g_dma_mtx_mul_dev.devnode,
                              DMA_MTX_MUL_BUF_BYTES,
                              g_dma_mtx_mul_dev.bufs[i].cpu_addr,
                              g_dma_mtx_mul_dev.bufs[i].dma_handle);
            g_dma_mtx_mul_dev.bufs[i].allocated = false;
            g_dma_mtx_mul_dev.bufs[i].cpu_addr = NULL;
            g_dma_mtx_mul_dev.bufs[i].dma_handle = (dma_addr_t)0;
            g_dma_mtx_mul_dev.bufs[i].num_elems = 0;
            g_dma_mtx_mul_dev.bufs[i].refcnt = 0;
        }
    }

    device_destroy(dma_mtx_mul_class, dma_mtx_mul_devno);
    class_destroy(dma_mtx_mul_class);
    cdev_del(&dma_mtx_mul_cdev);
    unregister_chrdev_region(dma_mtx_mul_devno, 1);

    pr_info("dma_mtx_mul: driver unloaded\n");
}

module_init(dma_mtx_mul_init);
module_exit(dma_mtx_mul_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Urban Klobcic");
MODULE_DESCRIPTION("This device driver simulates matrix multiplier hardware accelerator and exposes it to the user.");
MODULE_VERSION("1.0");