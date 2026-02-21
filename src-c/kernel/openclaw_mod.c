/*
 * openclaw_mod.c — OpenClaw Linux kernel module
 *
 * Provides /dev/oclaw chardev for userspace IPC, /proc/openclaw for stats,
 * and netfilter hooks for network-aware AI.
 *
 * Build: make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 * Load:  sudo insmod openclaw.ko
 * Unload: sudo rmmod openclaw
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/time64.h>
#include <linux/ktime.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include "openclaw.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenClaw Project");
MODULE_DESCRIPTION("OpenClaw AI Assistant Kernel Module");
MODULE_VERSION("0.1.0");

/* Module parameters */
static int log_level = 1;  /* 0=quiet, 1=info, 2=debug */
module_param(log_level, int, 0644);
MODULE_PARM_DESC(log_level, "Log verbosity (0=quiet, 1=info, 2=debug)");

static int enable_netfilter = 1;
module_param(enable_netfilter, int, 0644);
MODULE_PARM_DESC(enable_netfilter, "Enable netfilter hooks (0=off, 1=on)");

/* Global state */
static dev_t            oclaw_dev;
static struct cdev      oclaw_cdev;
static struct class    *oclaw_class;
static struct device   *oclaw_device;
static DEFINE_MUTEX(oclaw_mutex);

/* Statistics */
static struct oclaw_stats stats;
static ktime_t module_start_time;
static atomic_t open_count = ATOMIC_INIT(0);

/* Message ring buffer for chardev IPC */
#define RING_SIZE 64
#define MSG_MAX_LEN OCLAW_MAX_MSG_SIZE

struct ring_msg {
    char   *data;
    size_t  len;
};

static struct ring_msg ring_buf[RING_SIZE];
static int ring_head;  /* Write position */
static int ring_tail;  /* Read position */
static DEFINE_SPINLOCK(ring_lock);
static DECLARE_WAIT_QUEUE_HEAD(read_queue);
static DECLARE_WAIT_QUEUE_HEAD(write_queue);

/* Forward declarations */
extern int  openclaw_chardev_open(struct inode *inode, struct file *file);
extern int  openclaw_chardev_release(struct inode *inode, struct file *file);
extern ssize_t openclaw_chardev_read(struct file *file, char __user *buf,
                                      size_t count, loff_t *ppos);
extern ssize_t openclaw_chardev_write(struct file *file, const char __user *buf,
                                       size_t count, loff_t *ppos);
extern long openclaw_chardev_ioctl(struct file *file, unsigned int cmd,
                                    unsigned long arg);

extern int  openclaw_procfs_init(void);
extern void openclaw_procfs_exit(void);

extern int  openclaw_netfilter_init(void);
extern void openclaw_netfilter_exit(void);

/* File operations */
static const struct file_operations oclaw_fops = {
    .owner          = THIS_MODULE,
    .open           = openclaw_chardev_open,
    .release        = openclaw_chardev_release,
    .read           = openclaw_chardev_read,
    .write          = openclaw_chardev_write,
    .unlocked_ioctl = openclaw_chardev_ioctl,
};

/* Exported symbols for sub-modules */
struct oclaw_stats *openclaw_get_stats(void) { return &stats; }
int openclaw_get_log_level(void) { return log_level; }
int openclaw_get_netfilter_enabled(void) { return enable_netfilter; }
ktime_t openclaw_get_start_time(void) { return module_start_time; }
atomic_t *openclaw_get_open_count(void) { return &open_count; }
struct mutex *openclaw_get_mutex(void) { return &oclaw_mutex; }

/* Ring buffer access */
struct ring_msg *openclaw_get_ring(void) { return ring_buf; }
int *openclaw_get_ring_head(void) { return &ring_head; }
int *openclaw_get_ring_tail(void) { return &ring_tail; }
spinlock_t *openclaw_get_ring_lock(void) { return &ring_lock; }
wait_queue_head_t *openclaw_get_read_queue(void) { return &read_queue; }
wait_queue_head_t *openclaw_get_write_queue(void) { return &write_queue; }

static int __init openclaw_init(void)
{
    int ret;

    pr_info("openclaw: initializing v%d.%d.%d\n",
            0, 1, 0);

    module_start_time = ktime_get_boottime();
    memset(&stats, 0, sizeof(stats));

    /* Allocate chardev region */
    ret = alloc_chrdev_region(&oclaw_dev, 0, 1, OPENCLAW_DEVICE_NAME);
    if (ret < 0) {
        pr_err("openclaw: failed to allocate chardev region: %d\n", ret);
        return ret;
    }

    /* Init and add cdev */
    cdev_init(&oclaw_cdev, &oclaw_fops);
    oclaw_cdev.owner = THIS_MODULE;
    ret = cdev_add(&oclaw_cdev, oclaw_dev, 1);
    if (ret < 0) {
        pr_err("openclaw: failed to add cdev: %d\n", ret);
        goto err_cdev;
    }

    /* Create device class */
    oclaw_class = class_create(OPENCLAW_CLASS_NAME);
    if (IS_ERR(oclaw_class)) {
        ret = PTR_ERR(oclaw_class);
        pr_err("openclaw: failed to create class: %d\n", ret);
        goto err_class;
    }

    /* Create device */
    oclaw_device = device_create(oclaw_class, NULL, oclaw_dev, NULL,
                                  OPENCLAW_DEVICE_NAME);
    if (IS_ERR(oclaw_device)) {
        ret = PTR_ERR(oclaw_device);
        pr_err("openclaw: failed to create device: %d\n", ret);
        goto err_device;
    }

    /* Initialize ring buffer */
    memset(ring_buf, 0, sizeof(ring_buf));
    ring_head = 0;
    ring_tail = 0;

    /* Initialize procfs */
    ret = openclaw_procfs_init();
    if (ret < 0) {
        pr_warn("openclaw: procfs init failed (non-fatal): %d\n", ret);
    }

    /* Initialize netfilter */
    if (enable_netfilter) {
        ret = openclaw_netfilter_init();
        if (ret < 0) {
            pr_warn("openclaw: netfilter init failed (non-fatal): %d\n", ret);
            enable_netfilter = 0;
        }
    }

    pr_info("openclaw: module loaded (major=%d, minor=%d)\n",
            MAJOR(oclaw_dev), MINOR(oclaw_dev));
    pr_info("openclaw: /dev/%s created\n", OPENCLAW_DEVICE_NAME);

    return 0;

err_device:
    class_destroy(oclaw_class);
err_class:
    cdev_del(&oclaw_cdev);
err_cdev:
    unregister_chrdev_region(oclaw_dev, 1);
    return ret;
}

static void __exit openclaw_exit(void)
{
    int i;

    pr_info("openclaw: unloading module\n");

    /* Cleanup netfilter */
    if (enable_netfilter) {
        openclaw_netfilter_exit();
    }

    /* Cleanup procfs */
    openclaw_procfs_exit();

    /* Free ring buffer messages */
    for (i = 0; i < RING_SIZE; i++) {
        kfree(ring_buf[i].data);
        ring_buf[i].data = NULL;
    }

    /* Destroy device */
    device_destroy(oclaw_class, oclaw_dev);
    class_destroy(oclaw_class);
    cdev_del(&oclaw_cdev);
    unregister_chrdev_region(oclaw_dev, 1);

    pr_info("openclaw: module unloaded\n");
}

module_init(openclaw_init);
module_exit(openclaw_exit);
