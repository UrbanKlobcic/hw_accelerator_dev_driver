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

#include "../include/mtx_mul_ioctl.h"

#define DEVICE_NAME "mtx_mul"

/* Fake control register bits */
#define MTX_MUL_CTRL_START   0x1

/* Fake status register values */
#define MTX_MUL_STATUS_IDLE  0x0
#define MTX_MUL_STATUS_BUSY  0x1
#define MTX_MUL_STATUS_DONE  0x2

/* Fake IRQ status bits */
#define MTX_MUL_IRQ_JOB_DONE 0x1

struct mtx_mul_regs {
    u32 control;
    u32 status;
    u32 irq_status;
};

struct mtx_mul_buf_obj {
    bool allocated;
    u32 num_elems;
    int32_t data[MTX_MUL_MAX_ELEMS];
};

struct mtx_mul_device {
    struct mtx_mul_regs regs;
    struct mtx_mul_buf_obj bufs[MTX_MUL_NUM_BUFS];

    struct mtx_mul_desc active_desc;
    bool desc_valid;
    bool result_ready;
    bool stop_worker;

    struct mutex lock;
    wait_queue_head_t wq;
    struct task_struct *worker_thread;
};

static dev_t mtx_mul_devno;
static struct cdev mtx_mul_cdev;
static struct class *mtx_mul_class;

static struct mtx_mul_device g_mtx_mul_dev;

static int mtx_mul_open(struct inode *inode, struct file *file)
{
    pr_info("mtx_mul: device opened\n");
    return 0;
}

static int mtx_mul_release(struct inode *inode, struct file *file)
{
    pr_info("mtx_mul: device closed\n");
    return 0;
}

static void mtx_mul_reset_regs(struct mtx_mul_device *dev)
{
    dev->regs.control = 0;
    dev->regs.status = MTX_MUL_STATUS_IDLE;
    dev->regs.irq_status = 0;
}

static bool mtx_mul_buf_valid(u32 buf_id)
{
    return buf_id < MTX_MUL_NUM_BUFS;
}

static int mtx_mul_validate_desc(const struct mtx_mul_desc *d)
{
    if (d->m == 0 || d->n == 0 || d->k == 0)
        return -EINVAL;

    if (d->m > MTX_MUL_MAX_DIM ||
        d->n > MTX_MUL_MAX_DIM ||
        d->k > MTX_MUL_MAX_DIM)
        return -EINVAL;

    if (!mtx_mul_buf_valid(d->buf_a) ||
        !mtx_mul_buf_valid(d->buf_b) ||
        !mtx_mul_buf_valid(d->buf_c))
        return -EINVAL;

    return 0;
}

/*
 * write(): load data into a buffer handle
 */
static ssize_t mtx_mul_write(struct file *file,
                             const char __user *buf,
                             size_t count,
                             loff_t *ppos)
{
    struct mtx_mul_buffer ubuf;

    if (count != sizeof(ubuf))
        return -EINVAL;

    if (*ppos != 0)
        return -ESPIPE;

    if (copy_from_user(&ubuf, buf, sizeof(ubuf)))
        return -EFAULT;

    if (!mtx_mul_buf_valid(ubuf.buf_id))
        return -EINVAL;

    if (ubuf.num_elems > MTX_MUL_MAX_ELEMS)
        return -EINVAL;

    mutex_lock(&g_mtx_mul_dev.lock);

    if (!g_mtx_mul_dev.bufs[ubuf.buf_id].allocated) {
        mutex_unlock(&g_mtx_mul_dev.lock);
        return -EINVAL;
    }

    g_mtx_mul_dev.bufs[ubuf.buf_id].num_elems = ubuf.num_elems;
    memcpy(g_mtx_mul_dev.bufs[ubuf.buf_id].data,
           ubuf.data,
           sizeof(ubuf.data));

    mutex_unlock(&g_mtx_mul_dev.lock);

    pr_info("mtx_mul: wrote %u elems into buffer %u\n",
            ubuf.num_elems, ubuf.buf_id);

    return sizeof(ubuf);
}

/*
 * read(): fetch data from a buffer handle
 * user preloads buf_id in the read buffer struct
 */
static ssize_t mtx_mul_read(struct file *file,
                            char __user *buf,
                            size_t count,
                            loff_t *ppos)
{
    struct mtx_mul_buffer ubuf;

    if (count != sizeof(ubuf))
        return -EINVAL;

    if (*ppos != 0)
        return -ESPIPE;

    if (copy_from_user(&ubuf, buf, sizeof(ubuf)))
        return -EFAULT;

    if (!mtx_mul_buf_valid(ubuf.buf_id))
        return -EINVAL;

    mutex_lock(&g_mtx_mul_dev.lock);

    if (!g_mtx_mul_dev.bufs[ubuf.buf_id].allocated) {
        mutex_unlock(&g_mtx_mul_dev.lock);
        return -EINVAL;
    }

    ubuf.num_elems = g_mtx_mul_dev.bufs[ubuf.buf_id].num_elems;
    memcpy(ubuf.data,
           g_mtx_mul_dev.bufs[ubuf.buf_id].data,
           sizeof(ubuf.data));

    mutex_unlock(&g_mtx_mul_dev.lock);

    if (copy_to_user(buf, &ubuf, sizeof(ubuf)))
        return -EFAULT;

    return sizeof(ubuf);
}

static int mtx_mul_worker_fn(void *data)
{
    struct mtx_mul_device *dev = data;

    pr_info("mtx_mul: worker thread started\n");

    while (!kthread_should_stop()) {
        struct mtx_mul_desc d;
        u32 i, j, p;

        wait_event_interruptible(dev->wq,
                                 dev->desc_valid || 
                                 dev->stop_worker || 
                                 kthread_should_stop());

        if (dev->stop_worker || kthread_should_stop())
            break;

        mutex_lock(&dev->lock);

        if (!dev->desc_valid) {
            mutex_unlock(&dev->lock);
            continue;
        }

        d = dev->active_desc;
        dev->regs.status = MTX_MUL_STATUS_BUSY;
        dev->regs.irq_status = 0;

        pr_info("mtx_mul: worker started desc A=%u B=%u C=%u dims=%ux%ux%u\n",
                d.buf_a, d.buf_b, d.buf_c, d.m, d.n, d.k);

        mutex_unlock(&dev->lock);

        msleep(200);

        mutex_lock(&dev->lock);

        memset(dev->bufs[d.buf_c].data, 0, sizeof(dev->bufs[d.buf_c].data));

        for (i = 0; i < d.m; i++) {
            for (j = 0; j < d.n; j++) {
                int32_t sum = 0;

                for (p = 0; p < d.k; p++) {
                    sum += dev->bufs[d.buf_a].data[i * d.k + p] *
                           dev->bufs[d.buf_b].data[p * d.n + j];
                }

                dev->bufs[d.buf_c].data[i * d.n + j] = sum;
            }
        }

        dev->bufs[d.buf_c].num_elems = d.m * d.n;

        dev->desc_valid = false;
        dev->result_ready = true;

        dev->regs.status = MTX_MUL_STATUS_DONE;
        dev->regs.irq_status = MTX_MUL_IRQ_JOB_DONE;
        dev->regs.control &= ~MTX_MUL_CTRL_START;

        pr_info("mtx_mul: worker completed desc\n");

        mutex_unlock(&dev->lock);

        wake_up_interruptible(&dev->wq);
    }

    pr_info("mtx_mul: worker thread stopping\n");
    return 0;
}

static long mtx_mul_handle_alloc_buf(unsigned long arg)
{
    u32 buf_id;
    int i;

    mutex_lock(&g_mtx_mul_dev.lock);

    for (i = 0; i < MTX_MUL_NUM_BUFS; i++) {
        if (!g_mtx_mul_dev.bufs[i].allocated) {
            g_mtx_mul_dev.bufs[i].allocated = true;
            g_mtx_mul_dev.bufs[i].num_elems = 0;
            memset(g_mtx_mul_dev.bufs[i].data, 0, sizeof(g_mtx_mul_dev.bufs[i].data));
            buf_id = i;
            mutex_unlock(&g_mtx_mul_dev.lock);

            if (copy_to_user((void __user *)arg, &buf_id, sizeof(buf_id)))
                return -EFAULT;

            pr_info("mtx_mul: allocated buffer %u\n", buf_id);
            wake_up_interruptible(&g_mtx_mul_dev.wq);
            return 0;
        }
    }

    mutex_unlock(&g_mtx_mul_dev.lock);
    return -ENOSPC;
}

static long mtx_mul_handle_free_buf(unsigned long arg)
{
    u32 buf_id;

    if (copy_from_user(&buf_id, (void __user *)arg, sizeof(buf_id)))
        return -EFAULT;

    if (!mtx_mul_buf_valid(buf_id))
        return -EINVAL;

    mutex_lock(&g_mtx_mul_dev.lock);

    if (!g_mtx_mul_dev.bufs[buf_id].allocated) {
        mutex_unlock(&g_mtx_mul_dev.lock);
        return -EINVAL;
    }

    g_mtx_mul_dev.bufs[buf_id].allocated = false;
    g_mtx_mul_dev.bufs[buf_id].num_elems = 0;
    memset(g_mtx_mul_dev.bufs[buf_id].data, 0, sizeof(g_mtx_mul_dev.bufs[buf_id].data));

    mutex_unlock(&g_mtx_mul_dev.lock);

    pr_info("mtx_mul: freed buffer %u\n", buf_id);
    wake_up_interruptible(&g_mtx_mul_dev.wq);
    return 0;
}

static long mtx_mul_handle_submit_desc(unsigned long arg)
{
    struct mtx_mul_desc d;
    int ret;

    if (copy_from_user(&d, (void __user *)arg, sizeof(d)))
        return -EFAULT;

    ret = mtx_mul_validate_desc(&d);
    if (ret)
        return ret;

    mutex_lock(&g_mtx_mul_dev.lock);

    if (g_mtx_mul_dev.desc_valid || g_mtx_mul_dev.regs.status == MTX_MUL_STATUS_BUSY) {
        mutex_unlock(&g_mtx_mul_dev.lock);
        return -EBUSY;
    }

    if (!g_mtx_mul_dev.bufs[d.buf_a].allocated ||
        !g_mtx_mul_dev.bufs[d.buf_b].allocated ||
        !g_mtx_mul_dev.bufs[d.buf_c].allocated) {
        mutex_unlock(&g_mtx_mul_dev.lock);
        return -EINVAL;
    }

    g_mtx_mul_dev.active_desc = d;
    g_mtx_mul_dev.desc_valid = true;
    g_mtx_mul_dev.result_ready = false;
    g_mtx_mul_dev.regs.control |= MTX_MUL_CTRL_START;
    g_mtx_mul_dev.regs.status = MTX_MUL_STATUS_BUSY;
    g_mtx_mul_dev.regs.irq_status = 0;

    mutex_unlock(&g_mtx_mul_dev.lock);

    pr_info("mtx_mul: submitted desc A=%u B=%u C=%u\n",
            d.buf_a, d.buf_b, d.buf_c);

    wake_up_interruptible(&g_mtx_mul_dev.wq);
    return 0;
}

static long mtx_mul_handle_wait(void)
{
    int ret;

    ret = wait_event_interruptible(
        g_mtx_mul_dev.wq,
        ({
            bool done;
            mutex_lock(&g_mtx_mul_dev.lock);
            done = g_mtx_mul_dev.result_ready &&
                   g_mtx_mul_dev.regs.status == MTX_MUL_STATUS_DONE &&
                   (g_mtx_mul_dev.regs.irq_status & MTX_MUL_IRQ_JOB_DONE);
            mutex_unlock(&g_mtx_mul_dev.lock);
            done;
        })
    );

    return ret;
}

static long mtx_mul_handle_get_buf_status(unsigned long arg)
{
    struct mtx_mul_buf_status st;

    if (copy_from_user(&st, (void __user *)arg, sizeof(st)))
        return -EFAULT;

    if (!mtx_mul_buf_valid(st.buf_id))
        return -EINVAL;

    mutex_lock(&g_mtx_mul_dev.lock);
    st.allocated = g_mtx_mul_dev.bufs[st.buf_id].allocated ? 1 : 0;
    mutex_unlock(&g_mtx_mul_dev.lock);

    if (copy_to_user((void __user *)arg, &st, sizeof(st)))
        return -EFAULT;

    return 0;
}

static long mtx_mul_handle_get_dev_status(unsigned long arg)
{
    struct mtx_mul_dev_status st;

    mutex_lock(&g_mtx_mul_dev.lock);
    st.control = g_mtx_mul_dev.regs.control;
    st.status = g_mtx_mul_dev.regs.status;
    st.irq_status = g_mtx_mul_dev.regs.irq_status;
    st.busy = (g_mtx_mul_dev.desc_valid || g_mtx_mul_dev.regs.status == MTX_MUL_STATUS_BUSY) ? 1 : 0;
    mutex_unlock(&g_mtx_mul_dev.lock);

    if (copy_to_user((void __user *)arg, &st, sizeof(st)))
        return -EFAULT;

    return 0;
}

static __poll_t mtx_mul_poll(struct file *file, poll_table *wait)
{
    __poll_t mask = 0;
    bool has_free = false;
    bool has_result = false;
    int i;

    poll_wait(file, &g_mtx_mul_dev.wq, wait);

    mutex_lock(&g_mtx_mul_dev.lock);

    for (i = 0; i < MTX_MUL_NUM_BUFS; i++) {
        if (!g_mtx_mul_dev.bufs[i].allocated)
            has_free = true;
    }

    if (g_mtx_mul_dev.result_ready)
        has_result = true;

    if (has_result)
        mask |= POLLIN | POLLRDNORM;

    if (has_free || g_mtx_mul_dev.regs.status == MTX_MUL_STATUS_IDLE)
        mask |= POLLOUT | POLLWRNORM;

    mutex_unlock(&g_mtx_mul_dev.lock);

    return mask;
}

static long mtx_mul_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case MTX_MUL_IOCTL_ALLOC_BUF:
        return mtx_mul_handle_alloc_buf(arg);
    case MTX_MUL_IOCTL_FREE_BUF:
        return mtx_mul_handle_free_buf(arg);
    case MTX_MUL_IOCTL_SUBMIT_DESC:
        return mtx_mul_handle_submit_desc(arg);
    case MTX_MUL_IOCTL_WAIT:
        return mtx_mul_handle_wait();
    case MTX_MUL_IOCTL_GET_BUF_STATUS:
        return mtx_mul_handle_get_buf_status(arg);
    case MTX_MUL_IOCTL_GET_DEV_STATUS:
        return mtx_mul_handle_get_dev_status(arg);
    default:
        return -ENOTTY;
    }
}

static const struct file_operations mtx_mul_fops = {
    .owner = THIS_MODULE,
    .open = mtx_mul_open,
    .release = mtx_mul_release,
    .read = mtx_mul_read,
    .write = mtx_mul_write,
    .unlocked_ioctl = mtx_mul_ioctl,
    .poll = mtx_mul_poll,
};

static int __init mtx_mul_init(void)
{
    int ret;
    int i;

    pr_info("mtx_mul: initializing matrix multiplier driver\n");

    mutex_init(&g_mtx_mul_dev.lock);
    init_waitqueue_head(&g_mtx_mul_dev.wq);

    g_mtx_mul_dev.desc_valid = false;
    g_mtx_mul_dev.result_ready = false;
    g_mtx_mul_dev.stop_worker = false;
    mtx_mul_reset_regs(&g_mtx_mul_dev);

    for (i = 0; i < MTX_MUL_NUM_BUFS; i++) {
        g_mtx_mul_dev.bufs[i].allocated = false;
        g_mtx_mul_dev.bufs[i].num_elems = 0;
        memset(g_mtx_mul_dev.bufs[i].data, 0, sizeof(g_mtx_mul_dev.bufs[i].data));
    }

    g_mtx_mul_dev.worker_thread = kthread_run(mtx_mul_worker_fn,
                                              &g_mtx_mul_dev,
                                              "mtx_mul_worker");
    if (IS_ERR(g_mtx_mul_dev.worker_thread)) {
        pr_err("mtx_mul: failed to create worker thread\n");
        return PTR_ERR(g_mtx_mul_dev.worker_thread);
    }

    ret = alloc_chrdev_region(&mtx_mul_devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("mtx_mul: failed to allocate char device region\n");
        kthread_stop(g_mtx_mul_dev.worker_thread);
        return ret;
    }

    cdev_init(&mtx_mul_cdev, &mtx_mul_fops);
    mtx_mul_cdev.owner = THIS_MODULE;

    ret = cdev_add(&mtx_mul_cdev, mtx_mul_devno, 1);
    if (ret < 0) {
        pr_err("mtx_mul: failed to add cdev\n");
        unregister_chrdev_region(mtx_mul_devno, 1);
        kthread_stop(g_mtx_mul_dev.worker_thread);
        return ret;
    }

    mtx_mul_class = class_create(DEVICE_NAME);
    if (IS_ERR(mtx_mul_class)) {
        pr_err("mtx_mul: failed to create class\n");
        cdev_del(&mtx_mul_cdev);
        unregister_chrdev_region(mtx_mul_devno, 1);
        kthread_stop(g_mtx_mul_dev.worker_thread);
        return PTR_ERR(mtx_mul_class);
    }

    if (IS_ERR(device_create(mtx_mul_class, NULL, mtx_mul_devno, NULL, DEVICE_NAME))) {
        pr_err("mtx_mul: failed to create device\n");
        class_destroy(mtx_mul_class);
        cdev_del(&mtx_mul_cdev);
        unregister_chrdev_region(mtx_mul_devno, 1);
        kthread_stop(g_mtx_mul_dev.worker_thread);
        return -1;
    }

    pr_info("mtx_mul: driver loaded successfully\n");
    pr_info("mtx_mul: major=%d minor=%d\n",
            MAJOR(mtx_mul_devno), MINOR(mtx_mul_devno));

    return 0;
}

static void __exit mtx_mul_exit(void)
{
    g_mtx_mul_dev.stop_worker = true;
    wake_up_interruptible(&g_mtx_mul_dev.wq);

    if (g_mtx_mul_dev.worker_thread)
        kthread_stop(g_mtx_mul_dev.worker_thread);

    device_destroy(mtx_mul_class, mtx_mul_devno);
    class_destroy(mtx_mul_class);
    cdev_del(&mtx_mul_cdev);
    unregister_chrdev_region(mtx_mul_devno, 1);

    pr_info("mtx_mul: driver unloaded\n");
}

module_init(mtx_mul_init);
module_exit(mtx_mul_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Urban Klobcic");
MODULE_VERSION("1.0");