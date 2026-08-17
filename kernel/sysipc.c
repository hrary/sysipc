#include <linux/module.h>      // THIS_MODULE, MODULE_LICENSE, module_init/exit
#include <linux/kernel.h>      // pr_info
#include <linux/fs.h>          // file_operations, inode, alloc_chrdev_region
#include <linux/cdev.h>        // cdev_init, cdev_add, cdev_del
#include <linux/device.h>      // class_create, device_create
#include <linux/sched.h>       // current
#include <linux/err.h>         // IS_ERR, PTR_ERR
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include "../common/ring.h"
#include <linux/poll.h>

#define BUF_ORDER  6            // 2^6 = 64 pages
#define BUF_PAGES  (1 << BUF_ORDER)
#define BUF_SIZE   (BUF_PAGES * PAGE_SIZE)

static DECLARE_WAIT_QUEUE_HEAD(sysipc_wq);
static struct page *buf_pages; // page is a pre-defined kernel struct
static void *buf_virt; // virtual address of the allocated buffer

static char *sysipc_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;
    return NULL;
}

static int sysipc_open(struct inode *inode, struct file *filp)
{
    pr_info("sysipc: open by pid %d\n", current->pid);
    return 0;
}

static int sysipc_release(struct inode *inode, struct file *filp)
{
    pr_info("sysipc: release by pid %d\n", current->pid);
    return 0;
}

static int sysipc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long len = vma->vm_end - vma->vm_start;
    unsigned long i, npages = len >> PAGE_SHIFT;
    int ret;

    pr_info("sysipc: mmap len=%lu pgoff=%lu BUF_SIZE=%lu\n", len, vma->vm_pgoff, (unsigned long)BUF_SIZE);

    if (len > BUF_SIZE)
        return -EINVAL;
    if (vma->vm_pgoff != 0) {
        return -EINVAL;
    }

    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_MIXEDMAP);

    for (i = 0; i < npages; i++) {
        ret = vm_insert_page(vma, vma->vm_start + (i << PAGE_SHIFT),
                            buf_pages + i);
        if (ret) {
            pr_err("sysipc: vm_insert_page failed at page %lu, ret=%d\n", i, ret);
            return ret;
        }
    }
    pr_info("sysipc: mapped %lu pages\n", npages);

    return 0;
}

static __poll_t sysipc_poll (struct file *filep, struct poll_table_struct *wait) {
    struct ring *r = (struct ring *)buf_virt;
    __poll_t mask = 0;
    poll_wait(filep, &sysipc_wq, wait);

    if (ring_readable(r)) mask |= EPOLLIN | EPOLLRDNORM;
    if (ring_writeable(r)) mask |= EPOLLOUT | EPOLLWRNORM;

    return mask;
}

static long sysipc_ioctl (struct file *filep, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
        case SYSIPC_KICK:
            wake_up_interruptible(&sysipc_wq);
            return 0;
        default:
            return -ENOTTY;
    }
}


// Define file operations for the sysipc device (vtable)
static const struct file_operations sysipc_fops = {
    .owner   = THIS_MODULE,
    .open    = sysipc_open,
    .release = sysipc_release,
    .mmap = sysipc_mmap,
    .poll = sysipc_poll,
    .unlocked_ioctl = sysipc_ioctl,
};

static dev_t devno; // device number for the sysipc device
static struct cdev sysipc_cdev;
static struct class *sysipc_class; 
// sysfs abstraction which makes an entry in /sys/class which causes
// udev to create device node /dev/sysipc automatically

// __init discards the section after module loads, saving memory
static int __init sysipc_init(void)
{
    int ret;
    /**
     * asks kernel for a free major number
     * 
     * major number: a number used by the kernel to identify the driver associated with a device
     * 
     * args: where to store result, first minor number, how many minors, name of the device
     */
    ret = alloc_chrdev_region(&devno, 0, 1, "sysipc");
    if (ret < 0)
        return ret;

    buf_pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, BUF_ORDER); // allocate 16 contiguous pages of memory, zeroed
    if (!buf_pages) {
        ret = -ENOMEM;
        goto err_region;
    }
    split_page(buf_pages, BUF_ORDER);

    buf_virt = page_address(buf_pages); // get the virtual address of the allocated pages

    /**
     * struct page * = the descriptor of the memory; what the kernel uses to manage the memory
     * page_address() result = the kernel virtual address of the allocated memory, which can be used to read/write to the memory
     * buf_virt = the virtual address of the allocated buffer, what the process sees after mmap
     */

    
    cdev_init(&sysipc_cdev, &sysipc_fops); // fills in the cdev struct and binds file operations
    ret = cdev_add(&sysipc_cdev, devno, 1); // adds the cdev to the kernel, making it live
    if (ret < 0)
        goto err_pages;

    // create a sysfs class and a device within it
    sysipc_class = class_create("sysipc");
    if (IS_ERR(sysipc_class)) {
        ret = PTR_ERR(sysipc_class);
        goto err_cdev;
    }

    sysipc_class->devnode = sysipc_devnode;

    if (IS_ERR(device_create(sysipc_class, NULL, devno, NULL, "sysipc"))) {
        ret = -ENODEV;
        goto err_class;
    }

    pr_info("sysipc: loaded, major %d\n", MAJOR(devno));
    pr_info("sysipc: allocated %lu bytes, page %p, virt %p\n", (unsigned long) BUF_SIZE, buf_pages, buf_virt);
    return 0;

// error handling: cleanup in reverse order of allocation if something above fails
err_class:
class_destroy(sysipc_class);
err_cdev:
    cdev_del(&sysipc_cdev);
err_pages:
    for (unsigned int i = 0; i < BUF_PAGES; i++)
        __free_page(buf_pages + i);
err_region:
    unregister_chrdev_region(devno, 1);
    return ret;
}

// __exit discards when the code is built into the kernel, saving memory
static void __exit sysipc_exit(void)
{
    device_destroy(sysipc_class, devno);
    class_destroy(sysipc_class);
    cdev_del(&sysipc_cdev);
    unregister_chrdev_region(devno, 1);
    for (unsigned int i = 0; i < BUF_PAGES; i++)
        __free_page(buf_pages + i);
    pr_info("sysipc: unloaded\n");
}


module_init(sysipc_init);
module_exit(sysipc_exit);
MODULE_LICENSE("GPL");