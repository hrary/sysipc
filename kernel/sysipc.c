#include <linux/module.h>      // THIS_MODULE, MODULE_LICENSE, module_init/exit
#include <linux/kernel.h>      // pr_info
#include <linux/fs.h>          // file_operations, inode, alloc_chrdev_region
#include <linux/cdev.h>        // cdev_init, cdev_add, cdev_del
#include <linux/device.h>      // class_create, device_create
#include <linux/sched.h>       // current
#include <linux/err.h>         // IS_ERR, PTR_ERR

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

// Define file operations for the sysipc device (vtable)
static const struct file_operations sysipc_fops = {
    .owner   = THIS_MODULE,
    .open    = sysipc_open,
    .release = sysipc_release,
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

    
    cdev_init(&sysipc_cdev, &sysipc_fops); // fills in the cdev struct and binds file operations
    ret = cdev_add(&sysipc_cdev, devno, 1); // adds the cdev to the kernel, making it live
    if (ret < 0)
        goto err_region;

    // create a sysfs class and a device within it
    sysipc_class = class_create("sysipc");
    if (IS_ERR(sysipc_class)) {
        ret = PTR_ERR(sysipc_class);
        goto err_cdev;
    }

    if (IS_ERR(device_create(sysipc_class, NULL, devno, NULL, "sysipc"))) {
        ret = -ENODEV;
        goto err_class;
    }

    pr_info("sysipc: loaded, major %d\n", MAJOR(devno));
    return 0;

    // error handling: cleanup in reverse order of allocation if something above fails
    err_class:
        class_destroy(sysipc_class);
    err_cdev:
        cdev_del(&sysipc_cdev);
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
    pr_info("sysipc: unloaded\n");
}


module_init(sysipc_init);
module_exit(sysipc_exit);
MODULE_LICENSE("GPL");