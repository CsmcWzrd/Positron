#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/types.h>

#include "../include/xchg_proto.h"
#include "xchg_ioctl_linux.h"

#define XCHG_DEVICE_NAME "xchg0"
#define XCHG_CLASS_NAME  "xchg"

struct xchg_region {
    u8 *buf;
    u32 region_size;
    u32 payload_size;
    u32 read_pos[2];
};

struct xchg_dev {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    struct mutex lock;
    wait_queue_head_t rdq;
    wait_queue_head_t wrq;

    xchg_config_t cfg;
    struct xchg_region ch_0_to_1;
    struct xchg_region ch_1_to_0;

    bool ep_open[2];
    u64 epoch_id;
    unsigned long epoch_jiffies;
    struct timer_list epoch_timer;
};

struct xchg_file_ctx {
    struct xchg_dev *dev;
    int role;
};

static struct xchg_dev *gdev;

static unsigned long xchg_compute_epoch_jiffies(u16 mantissa, int exponent)
{
    double log10_t, period_s;
    unsigned long j;

    if (mantissa == 0)
        return HZ;

    if (exponent < XCHG_MIN_EXPONENT)
        exponent = XCHG_MIN_EXPONENT;
    if (exponent > XCHG_MAX_EXPONENT)
        exponent = XCHG_MAX_EXPONENT;

    log10_t = log10((double)mantissa) + (double)exponent;
    if (log10_t < log10(1.0 / HZ))
        return 1;
    if (log10_t > log10(XCHG_MAX_PERIOD_SEC))
        return 86400UL * HZ;

    period_s = pow(10.0, log10_t);
    j = (unsigned long)(period_s * HZ);
    return j ? j : 1;
}

static void xchg_region_reset_epoch(struct xchg_region *r)
{
    r->read_pos[0] = 0;
    r->read_pos[1] = 0;
}

static int xchg_region_alloc(struct xchg_region *r, u32 sz)
{
    u8 *p;

    if (sz < sizeof(u32))
        return -EINVAL;

    p = kzalloc(sz, GFP_KERNEL);
    if (!p)
        return -ENOMEM;

    kfree(r->buf);
    r->buf = p;
    r->region_size = sz;
    r->payload_size = 0;
    xchg_region_reset_epoch(r);
    *(u32 *)r->buf = 0;
    return 0;
}

static inline void xchg_region_publish_size(struct xchg_region *r)
{
    if (r->buf && r->region_size >= sizeof(u32))
        *(u32 *)r->buf = r->payload_size;
}

static void xchg_advance_epoch_locked(struct xchg_dev *d)
{
    d->epoch_id++;
    xchg_region_reset_epoch(&d->ch_0_to_1);
    xchg_region_reset_epoch(&d->ch_1_to_0);
    mod_timer(&d->epoch_timer, jiffies + d->epoch_jiffies);
    wake_up_all(&d->rdq);
    wake_up_all(&d->wrq);
}

static void xchg_epoch_timer_cb(struct timer_list *t)
{
    struct xchg_dev *d = from_timer(d, t, epoch_timer);
    mutex_lock(&d->lock);
    xchg_advance_epoch_locked(d);
    mutex_unlock(&d->lock);
}

static inline struct xchg_region *tx_region_for_role(struct xchg_dev *d, int role)
{
    return (role == 0) ? &d->ch_0_to_1 : &d->ch_1_to_0;
}

static inline struct xchg_region *rx_region_for_role(struct xchg_dev *d, int role)
{
    return (role == 0) ? &d->ch_1_to_0 : &d->ch_0_to_1;
}

static int xchg_open(struct inode *inode, struct file *filp)
{
    struct xchg_dev *d = container_of(inode->i_cdev, struct xchg_dev, cdev);
    struct xchg_file_ctx *ctx;

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    mutex_lock(&d->lock);
    if (!d->ep_open[0]) {
        d->ep_open[0] = true;
        ctx->role = 0;
    } else if (!d->ep_open[1]) {
        d->ep_open[1] = true;
        ctx->role = 1;
    } else {
        mutex_unlock(&d->lock);
        kfree(ctx);
        return -EBUSY;
    }
    ctx->dev = d;
    filp->private_data = ctx;
    mutex_unlock(&d->lock);
    return 0;
}

static int xchg_release(struct inode *inode, struct file *filp)
{
    struct xchg_file_ctx *ctx = filp->private_data;
    if (ctx) {
        mutex_lock(&ctx->dev->lock);
        ctx->dev->ep_open[ctx->role] = false;
        mutex_unlock(&ctx->dev->lock);
        kfree(ctx);
    }
    return 0;
}

static ssize_t xchg_read(struct file *filp, char __user *ubuf, size_t len, loff_t *offp)
{
    struct xchg_file_ctx *ctx = filp->private_data;
    struct xchg_region *r;
    u32 *posp, available, to_copy;
    ssize_t ret = 0;
    (void)offp;

    mutex_lock(&ctx->dev->lock);
    r = rx_region_for_role(ctx->dev, ctx->role);
    posp = &r->read_pos[ctx->role];

    if (r->payload_size == 0 || *posp >= r->payload_size)
        goto out;

    available = r->payload_size - *posp;
    to_copy = (len < available) ? (u32)len : available;

    if (copy_to_user(ubuf, r->buf + sizeof(u32) + *posp, to_copy)) {
        ret = -EFAULT;
        goto out;
    }

    *posp += to_copy;
    ret = to_copy;
out:
    mutex_unlock(&ctx->dev->lock);
    return ret;
}

static ssize_t xchg_write(struct file *filp, const char __user *ubuf, size_t len, loff_t *offp)
{
    struct xchg_file_ctx *ctx = filp->private_data;
    struct xchg_region *r;
    u32 capacity, to_copy;
    ssize_t ret = 0;
    int peer_role = (ctx->role == 0) ? 1 : 0;
    (void)offp;

    mutex_lock(&ctx->dev->lock);
    r = tx_region_for_role(ctx->dev, ctx->role);
    if (r->region_size <= sizeof(u32)) {
        ret = -ENOSPC;
        goto out;
    }

    capacity = r->region_size - sizeof(u32);
    to_copy = (len < capacity) ? (u32)len : capacity;

    if (copy_from_user(r->buf + sizeof(u32), ubuf, to_copy)) {
        ret = -EFAULT;
        goto out;
    }

    r->payload_size = to_copy;
    xchg_region_publish_size(r);
    r->read_pos[peer_role] = 0;
    wake_up_all(&ctx->dev->rdq);
    ret = to_copy;
out:
    mutex_unlock(&ctx->dev->lock);
    return ret;
}

static __poll_t xchg_poll(struct file *filp, poll_table *wait)
{
    struct xchg_file_ctx *ctx = filp->private_data;
    struct xchg_region *r;
    __poll_t mask = POLLOUT | POLLWRNORM;

    poll_wait(filp, &ctx->dev->rdq, wait);

    mutex_lock(&ctx->dev->lock);
    r = rx_region_for_role(ctx->dev, ctx->role);
    if (r->payload_size > 0 && r->read_pos[ctx->role] < r->payload_size)
        mask |= POLLIN | POLLRDNORM;
    mutex_unlock(&ctx->dev->lock);
    return mask;
}

static long xchg_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct xchg_file_ctx *ctx = filp->private_data;
    struct xchg_dev *d = ctx->dev;
    long ret = 0;

    switch (cmd) {
    case XCHG_IOC_SET_CONFIG: {
        xchg_config_t cfg;
        if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
            return -EFAULT;
        if (cfg.exponent < XCHG_MIN_EXPONENT || cfg.exponent > XCHG_MAX_EXPONENT)
            return -EINVAL;
        if (cfg.send_region_size < sizeof(u32) || cfg.recv_region_size < sizeof(u32))
            return -EINVAL;

        mutex_lock(&d->lock);
        ret = xchg_region_alloc(&d->ch_0_to_1, cfg.send_region_size);
        if (!ret)
            ret = xchg_region_alloc(&d->ch_1_to_0, cfg.recv_region_size);
        if (!ret) {
            d->cfg = cfg;
            d->epoch_id = 0;
            d->epoch_jiffies = xchg_compute_epoch_jiffies(cfg.mantissa, cfg.exponent);
            mod_timer(&d->epoch_timer, jiffies + d->epoch_jiffies);
        }
        mutex_unlock(&d->lock);
        return ret;
    }
    case XCHG_IOC_GET_STATUS: {
        xchg_status_t st;
        mutex_lock(&d->lock);
        st.mantissa = d->cfg.mantissa;
        st.reserved0 = 0;
        st.exponent = d->cfg.exponent;
        st.send_region_size = d->cfg.send_region_size;
        st.recv_region_size = d->cfg.recv_region_size;
        st.tx_payload_size = tx_region_for_role(d, ctx->role)->payload_size;
        st.rx_payload_size = rx_region_for_role(d, ctx->role)->payload_size;
        st.epoch_id = d->epoch_id;
        mutex_unlock(&d->lock);
        if (copy_to_user((void __user *)arg, &st, sizeof(st)))
            return -EFAULT;
        return 0;
    }
    case XCHG_IOC_RESET:
        mutex_lock(&d->lock);
        d->epoch_id = 0;
        d->ch_0_to_1.payload_size = 0;
        d->ch_1_to_0.payload_size = 0;
        xchg_region_publish_size(&d->ch_0_to_1);
        xchg_region_publish_size(&d->ch_1_to_0);
        xchg_region_reset_epoch(&d->ch_0_to_1);
        xchg_region_reset_epoch(&d->ch_1_to_0);
        mutex_unlock(&d->lock);
        return 0;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations xchg_fops = {
    .owner          = THIS_MODULE,
    .open           = xchg_open,
    .release        = xchg_release,
    .read           = xchg_read,
    .write          = xchg_write,
    .poll           = xchg_poll,
    .unlocked_ioctl = xchg_ioctl,
    .llseek         = no_llseek,
};

static int __init xchg_init(void)
{
    int ret;
    gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
    if (!gdev)
        return -ENOMEM;

    mutex_init(&gdev->lock);
    init_waitqueue_head(&gdev->rdq);
    init_waitqueue_head(&gdev->wrq);
    timer_setup(&gdev->epoch_timer, xchg_epoch_timer_cb, 0);

    gdev->cfg.mantissa = 1;
    gdev->cfg.exponent = 0;
    gdev->cfg.send_region_size = 4096;
    gdev->cfg.recv_region_size = 4096;
    gdev->epoch_jiffies = HZ;

    ret = xchg_region_alloc(&gdev->ch_0_to_1, gdev->cfg.send_region_size);
    if (ret) goto err0;
    ret = xchg_region_alloc(&gdev->ch_1_to_0, gdev->cfg.recv_region_size);
    if (ret) goto err1;

    ret = alloc_chrdev_region(&gdev->devno, 0, 1, XCHG_DEVICE_NAME);
    if (ret) goto err2;

    cdev_init(&gdev->cdev, &xchg_fops);
    gdev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&gdev->cdev, gdev->devno, 1);
    if (ret) goto err3;

    gdev->class = class_create(XCHG_CLASS_NAME);
    if (IS_ERR(gdev->class)) {
        ret = PTR_ERR(gdev->class);
        goto err4;
    }

    gdev->device = device_create(gdev->class, NULL, gdev->devno, NULL, XCHG_DEVICE_NAME);
    if (IS_ERR(gdev->device)) {
        ret = PTR_ERR(gdev->device);
        goto err5;
    }

    mod_timer(&gdev->epoch_timer, jiffies + gdev->epoch_jiffies);
    pr_info("xchg: loaded\n");
    return 0;
err5:
    class_destroy(gdev->class);
err4:
    cdev_del(&gdev->cdev);
err3:
    unregister_chrdev_region(gdev->devno, 1);
err2:
    kfree(gdev->ch_1_to_0.buf);
err1:
    kfree(gdev->ch_0_to_1.buf);
err0:
    kfree(gdev);
    return ret;
}

static void __exit xchg_exit(void)
{
    del_timer_sync(&gdev->epoch_timer);
    device_destroy(gdev->class, gdev->devno);
    class_destroy(gdev->class);
    cdev_del(&gdev->cdev);
    unregister_chrdev_region(gdev->devno, 1);
    kfree(gdev->ch_0_to_1.buf);
    kfree(gdev->ch_1_to_0.buf);
    kfree(gdev);
    pr_info("xchg: unloaded\n");
}

module_init(xchg_init);
module_exit(xchg_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI");
MODULE_DESCRIPTION("Timed two-endpoint exchange char device");
