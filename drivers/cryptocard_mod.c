#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include<linux/slab.h>                 //kmalloc()
#include<linux/uaccess.h>              //copy_to/from_user()
#include<linux/sysfs.h> 
#include<linux/kobject.h> 
#include<linux/delay.h> 

#define MY_DRIVER "my_pci_driver"
#define IDENTIFICATION 0x10C5730

#define OFFSET_IDENTIFICATION 0x0
#define OFFSET_LIVENESS 0x4
#define OFFSET_KEY_A 0xa
#define OFFSET_KEY_B 0xb
#define OFFSET_MMIO_LENGTH 0xc
#define OFFSET_MMIO_STATUS 0x20
#define OFFSET_MMIO_DATA_ADDRESS 0x80
#define OFFSET_DATA 0xa8

u16 vendor, device;

/* This sample driver supports device with VID = 0x010F, and PID = 0x0F0E*/
static struct pci_device_id my_driver_id_table[] = {
    {PCI_DEVICE(0x1234, 0xdeba)},
    {0,}
};

MODULE_DEVICE_TABLE(pci, my_driver_id_table);

static int my_driver_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void my_driver_remove(struct pci_dev *pdev);
static void release_device(struct pci_dev *pdev);
static int check_identification(struct pci_dev *pdev);
static int set_up_device(struct pci_dev *pdev);
static int __init mypci_driver_init(void);
static void __exit mypci_driver_exit(void);
static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count);
static int setup_sysfs(void);
static void cleanup(void);
static int cdev_open(struct inode *inode, struct file *file);
static int cdev_release(struct inode *inode, struct file *file);
static ssize_t cdev_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t cdev_write(struct file *filp, const char *buf, size_t len, loff_t * off);
static int setup_char_device(void);
static void set_key(void);
static void mmio_encrypt(void);
static void write_to_device(void);
static void read_from_device(void);

/* Driver registration structure */
static struct pci_driver my_driver = {
    .name = MY_DRIVER,
    .id_table = my_driver_id_table,
    .probe = my_driver_probe,
    .remove = my_driver_remove
};

static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = cdev_read,
        .write          = cdev_write,
        .open           = cdev_open,
        .release        = cdev_release,
};

dev_t dev = 0;
static struct class *dev_class;
static struct cdev cryptcard_cdev;
static u8 __iomem *MEM;
 char *kbuf;
static u32 klen;

static volatile u8 INTERRUPT = 0;
static volatile u8 DMA = 0;
static volatile u8 KEY_A = 0;
static volatile u8 KEY_B = 0;
struct kobject *kobj_ref;
struct kobj_attribute dma_attr = __ATTR(DMA, 0660, sysfs_show, sysfs_store);
struct kobj_attribute interrupt_attr = __ATTR(INTERRUPT, 0660, sysfs_show, sysfs_store);
struct kobj_attribute keya_attr = __ATTR(KEY_A, 0660, sysfs_show, sysfs_store);
struct kobj_attribute keyb_attr = __ATTR(KEY_B, 0660, sysfs_show, sysfs_store);
static struct attribute *attrs[] = {
	&dma_attr.attr,
	&interrupt_attr.attr,
    &keya_attr.attr,
    &keyb_attr.attr,
	NULL,
};
static struct attribute_group attr_group = {.attrs = attrs};

static void release_device(struct pci_dev *pdev)
{
    pci_disable_device(pdev);
    /* Free memory region */
    pci_release_region(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
}

static int check_identification(struct pci_dev *pdev)
{
    unsigned int identification = ioread32(MEM+OFFSET_IDENTIFICATION);
    if (identification != IDENTIFICATION)
    {
        pr_err("invalid identification 0x%X", identification);
        return -1;
    }
    pr_info("identification verified 0x%X", identification);
    return 0;
}

static int check_liveness(struct pci_dev *pdev)
{
    u32 num = 0x65485BAF;
    iowrite32(num,MEM+4);
    u32 num_device = ioread32(MEM+OFFSET_LIVENESS);
    if (num_device != ~num)
    {
        pr_err("liveness check failed, num 0x%X ~num 0x%X device 0x%X\n", num, ~num, num_device);
        return -1;
    }
    pr_info("liveness check successful\n");
    return 0;
}

static int set_up_device(struct pci_dev *pdev)
{
    int bar, err;
    unsigned long mmio_start, mmio_len;
    pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(pdev, PCI_DEVICE_ID, &device);
    pr_info("Registered: Device vid: 0x%X pid: 0x%X\n", vendor, device);
    bar = pci_select_bars(pdev, IORESOURCE_MEM);
    err = pci_enable_device_mem(pdev);
    if (err)
        return err;
    err = pci_request_region(pdev, bar, MY_DRIVER);
    if (err)
    {
        pci_disable_device(pdev);
        return err;
    }
    mmio_start = pci_resource_start(pdev, 0);
    mmio_len = pci_resource_len(pdev, 0);
    pr_info("mmio_start 0x%lX mmio_len 0x%lX\n", mmio_start, mmio_len);
    MEM = ioremap(mmio_start, mmio_len);
    if (!MEM)
    {
        release_device(pdev);
        return -EIO;
    }
    if (check_identification(pdev) < 0) return -1;
    if(check_liveness(pdev)<0) return -1;
    u32 val = ioread32(MEM+OFFSET_MMIO_STATUS);
    pr_info("mmio status register %u", val);
    return 0;
}


static int setup_sysfs(void) {
    kobj_ref = kobject_create_and_add("cryptcard_sysfs",kernel_kobj);
    if(sysfs_create_group(kobj_ref, &attr_group)) {
        kobject_put(kobj_ref);
        return -1;
    }
    pr_info("sysfs installed Successfully\n");
    return 0;
}

static int __init mypci_driver_init(void)
{
    int status = pci_register_driver(&my_driver);
    if (status < 0)
    {
        pr_err("cannot register driver, status %d\n", status);
    }
    status = setup_sysfs();
    if (status < 0)
    {
        pr_err("error creating sysfs, status %d\n", status);
    }
    status = setup_char_device();
    if (status < 0)
    {
        pr_err("error creating char device, status %d\n", status);
    }
    return 0;
}

static void __exit mypci_driver_exit(void)
{
    cleanup();
    pci_unregister_driver(&my_driver);
}

static void cleanup(void) {
    kobject_put(kobj_ref);
    device_destroy(dev_class,dev);
    class_destroy(dev_class);
    cdev_del(&cryptcard_cdev);
    unregister_chrdev_region(dev, 1);
}

/* This function is called by the kernel */
static int my_driver_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int status = set_up_device(pdev);
    if (status < 0)
    {
        pr_err("driver error, status %d\n", status);
    }
    return 0;
}

/* Clean up */
static void my_driver_remove(struct pci_dev *pdev)
{
    release_device(pdev);
    pr_info("Removed: Device vid: 0x%X pid: 0x%X\n", vendor, device);
}

static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    pr_info("sysfs_show %s", attr->attr.name);
	u8 val;
	if (strcmp(attr->attr.name, "DMA") == 0) val = DMA;
	else if (strcmp(attr->attr.name, "INTERRUPT") == 0) val = INTERRUPT;
    else if (strcmp(attr->attr.name, "KEY_A") == 0) val = KEY_A;
    else if (strcmp(attr->attr.name, "KEY_B") == 0) val = KEY_B;
	return sprintf(buf, "%d\n", val);
}

static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	u8 val = *buf;
    pr_info("sysfs_store %s %hhu", attr->attr.name, val);
    if ((strcmp(attr->attr.name, "DMA") == 0) || (strcmp(attr->attr.name, "INTERRUPT") == 0) ) {
        if(!(val==0 || val==1)) {
            pr_err("invalid value, can only be 0 or 1, recieved %hhu", val);
            return -EINVAL;
        }
        if (strcmp(attr->attr.name, "DMA") == 0) DMA = val;
	    else INTERRUPT = val;
    }
	else {     
        if (strcmp(attr->attr.name, "KEY_A") == 0) {
            KEY_A = val;
            set_key();
        }
        else {
            KEY_B = val;
            set_key();   
         
        }
    }
	return count;
}

static int cdev_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}
/*
** This function will be called when we close the Device file
*/ 
static int cdev_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}
 
/*
** This function will be called when we read the Device file
*/
static ssize_t cdev_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    if(copy_to_user(buf, kbuf, len) == 0) {
        pr_info("Read: %s",kbuf);  
        return len;
    }
    return -1;
}
/*
** This function will be called when we write the Device file
*/
static ssize_t cdev_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    klen = len;
    kbuf = kmalloc(klen+1, GFP_KERNEL);
    if(copy_from_user(kbuf, buf, len) == 0) {
        pr_info("Write: %s",kbuf);  
        pr_info("%zu %zu %p\n", sizeof(char*), sizeof(kbuf), kbuf);
        mmio_encrypt();
        return len;
    }
    return -1;
}

static int setup_char_device(void) {
    if((alloc_chrdev_region(&dev, 0, 1, "cryptcard_dev")) <0){
        pr_err("Cannot allocate major number for device\n");
        return -1;
    }
    pr_info("Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
    cdev_init(&cryptcard_cdev,&fops);
    if((cdev_add(&cryptcard_cdev,dev,1)) < 0){
        pr_err("Cannot add the device to the system\n");
        goto r_class;
    }
    if((dev_class = class_create(THIS_MODULE,"cryptcard_class")) == NULL){
        pr_err("Cannot create the struct class for device\n");
        goto r_class;
    }
    if((device_create(dev_class,NULL,dev,NULL,"cryptcard_device")) == NULL){
        pr_err("Cannot create the Device\n");
        goto r_device;
    }
    pr_info("Kernel Module Inserted Successfully\n");
    return 0;
r_device:
    class_destroy(dev_class);
r_class:
    unregister_chrdev_region(dev,1);
    return -1;
}

static void set_key(void) {
    iowrite32((KEY_A << 8) | KEY_B,MEM+0x8);
}

static void write_to_device(void) {
    int i;
    for (i = 0; i < ((klen+4)/4); i++) {
        u32 *cur = (u32 *)kbuf;
        u32 pos = OFFSET_DATA + i * 4;
        cur += i;
        iowrite32(*cur, MEM + pos);
    }
}

static void read_from_device(void) {
    int i;
    for (i = 0; i < ((klen+4)/4); i++) {
        u32 pos = OFFSET_DATA + i * 4;
        u32 val = ioread32(MEM + pos);
        u32 *cur = (u32 *)kbuf;
        cur += i;
        *cur = val;
    }
}

static void mmio_encrypt(void) {
    iowrite32(klen,MEM+OFFSET_MMIO_LENGTH);
    write_to_device();
    iowrite32(OFFSET_DATA,MEM+OFFSET_MMIO_DATA_ADDRESS);
    u32 val = ioread32(MEM+OFFSET_MMIO_STATUS);
    do {
        val = ioread32(MEM+OFFSET_MMIO_STATUS);
    } while(val==1);
    read_from_device();
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ruke");
MODULE_DESCRIPTION("Test PCI driver");
MODULE_VERSION("0.1");

module_init(mypci_driver_init);
module_exit(mypci_driver_exit);