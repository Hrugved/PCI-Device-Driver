#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>    //kmalloc()
#include <linux/uaccess.h> //copy_to/from_user()
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/mutex.h>

#define MY_DRIVER "CRYPTOCARD_PCI_DRIVER"
#define IDENTIFICATION 0x10C5730
#define IRQ_NO 11
#define INTERRUPT_MMIO 0x001
#define INTERRUPT_DMA 0x100
#define DATA_SIZE 1048408
#define DMA_DATA_SIZE 32768

#define OFFSET_IDENTIFICATION 0x0
#define OFFSET_LIVENESS 0x4
#define OFFSET_KEY_A 0xa
#define OFFSET_KEY_B 0xb
#define OFFSET_MMIO_LENGTH 0xc
#define OFFSET_MMIO_STATUS 0x20
#define OFFSET_MMIO_DATA_ADDRESS 0x80
#define OFFSET_DATA 0xa8
#define OFFSET_INTERRUPT_STATUS 0x24
#define OFFSET_INTERRUPT_ACK 0x64
#define OFFSET_DMA_DATA_ADDRESS 0x90
#define OFFSET_DMA_LENGTH 0x98
#define OFFSET_DMA_COMMAND 0xa0

/* This sample driver supports device with VID = 0x010F, and PID = 0x0F0E*/
static struct pci_device_id my_driver_id_table[] = {
    {PCI_DEVICE(0x1234, 0xdeba)},
    {
        0,
    }};
MODULE_DEVICE_TABLE(pci, my_driver_id_table);

/** Function Prototypes */
static int my_driver_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void my_driver_remove(struct pci_dev *pdev);
static void release_device(struct pci_dev *pdev);
static int check_identification(struct pci_dev *pdev);
static int setup_device(struct pci_dev *pdev);
static int __init mypci_driver_init(void);
static void __exit mypci_driver_exit(void);
static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t sysfs_show_tid(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store_tid(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
static int setup_sysfs(void);
static void cleanup(void);
static int cdev_open(struct inode *inode, struct file *file);
static int cdev_release(struct inode *inode, struct file *file);
static ssize_t cdev_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t cdev_write(struct file *filp, const char *buf, size_t len, loff_t *off);
static int cdev_mmap(struct file *filp, struct vm_area_struct *vma);
static int setup_char_device(void);
static void set_key(void);
static void mmio(void);
static void write_to_device(void);
static void read_from_device(void);
static irqreturn_t irq_handler(int irq, void *cookie);
static int set_interrupts(struct pci_dev *pdev);
static void dma(void);

/* CDEV */
static struct pci_driver my_driver = {
    .name = MY_DRIVER,
    .id_table = my_driver_id_table,
    .probe = my_driver_probe,
    .remove = my_driver_remove};

static struct file_operations fops =
    {
        .owner = THIS_MODULE,
        .read = cdev_read,
        .write = cdev_write,
        .open = cdev_open,
        .release = cdev_release,
        .mmap = cdev_mmap
};

dev_t dev = 0;
static struct class *dev_class;
static struct cdev cryptcard_cdev;
static u64 klen;
static u8 __iomem *MEM;
char *mmio_buf;
unsigned long mmio_start, mmio_len;
char *dma_buf;
dma_addr_t dma_handle;
DEFINE_MUTEX(dev_lock);
static volatile int is_data_ready;
DECLARE_WAIT_QUEUE_HEAD(wq_is_data_ready);
u16 vendor, device;
static char *NA = "N/A";

/** SYSFS */
static volatile u8 INTERRUPT = 0;
static volatile u8 DMA = 0;
static volatile u8 KEY_A = 0;
static volatile u8 KEY_B = 0;
static volatile u8 DECRYPT = 0;
static volatile u8 IS_MAPPED = 0;
static volatile int TID = 0;
struct kobject *kobj_ref;
struct kobj_attribute dma_attr = __ATTR(DMA, 0660, sysfs_show, sysfs_store);
struct kobj_attribute interrupt_attr = __ATTR(INTERRUPT, 0660, sysfs_show, sysfs_store);
struct kobj_attribute keya_attr = __ATTR(KEY_A, 0660, sysfs_show, sysfs_store);
struct kobj_attribute keyb_attr = __ATTR(KEY_B, 0660, sysfs_show, sysfs_store);
struct kobj_attribute decrypt_attr = __ATTR(DECRYPT, 0660, sysfs_show, sysfs_store);
struct kobj_attribute isMapped_attr = __ATTR(IS_MAPPED, 0660, sysfs_show, sysfs_store);
struct kobj_attribute tid_attr = __ATTR(TID, 0660, sysfs_show_tid, sysfs_store_tid);
static struct attribute *attrs[] = {
    &dma_attr.attr,
    &interrupt_attr.attr,
    &keya_attr.attr,
    &keyb_attr.attr,
    &decrypt_attr.attr,
    &isMapped_attr.attr,
    NULL,
};
static struct attribute_group attr_group = {.attrs = attrs};

static void release_device(struct pci_dev *pdev)
{
    free_irq(IRQ_NO, (void *)(irq_handler));
    pci_release_region(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
    pci_disable_device(pdev);
}

static int check_identification(struct pci_dev *pdev)
{
    unsigned int identification = ioread32(MEM + OFFSET_IDENTIFICATION);
    if (identification != IDENTIFICATION)
    {
        pr_err("CRYPTOCARD_MOD ---> invalid identification 0x%X", identification);
        return -1;
    }
    pr_info("CRYPTOCARD_MOD ---> identification verified 0x%X", identification);
    return 0;
}

static int check_liveness(struct pci_dev *pdev)
{
    u32 num = 0x65485BAF;
    iowrite32(num, MEM + 4);
    u32 num_device = ioread32(MEM + OFFSET_LIVENESS);
    if (num_device != ~num)
    {
        pr_err("CRYPTOCARD_MOD ---> liveness check failed, num 0x%X ~num 0x%X device 0x%X\n", num, ~num, num_device);
        return -1;
    }
    pr_info("CRYPTOCARD_MOD ---> liveness check successful\n");
    return 0;
}

static int setup_device(struct pci_dev *pdev)
{
    int bar, err;
    pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(pdev, PCI_DEVICE_ID, &device);
    pr_info("CRYPTOCARD_MOD ---> Registered: Device vid: 0x%X pid: 0x%X\n", vendor, device);
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
    pr_info("CRYPTOCARD_MOD ---> mmio_start 0x%lX mmio_len 0x%lX\n", mmio_start, mmio_len);
    MEM = ioremap(mmio_start, mmio_len);
    if (!MEM)
    {
        release_device(pdev);
        return -EIO;
    }
    mmio_buf = kmalloc(DATA_SIZE, GFP_KERNEL);
    if (mmio_buf==NULL) {
        pr_err("CRYPTOCARD_MOD ---> failed to allocate mmio_buf\n");
        return -EIO;
    }
    dma_buf = dma_alloc_coherent(&(pdev->dev), DMA_DATA_SIZE, &dma_handle, GFP_KERNEL);
    if (dma_buf==NULL) {
        pr_err("CRYPTOCARD_MOD ---> failed to allocate coherent buffer\n");
        return -EIO;
    }
    if (check_identification(pdev) < 0)
        return -1;
    if (check_liveness(pdev) < 0)
        return -1;
    return set_interrupts(pdev);
}

static int setup_sysfs(void)
{
    kobj_ref = kobject_create_and_add("cryptocard_sysfs", kernel_kobj);
    if ((sysfs_create_group(kobj_ref, &attr_group)) || (sysfs_create_file(kobj_ref,&tid_attr.attr)))
    {
        kobject_put(kobj_ref);
        return -1;
    }
    pr_info("CRYPTOCARD_MOD ---> sysfs installed Successfully\n");
    return 0;
}

static int __init mypci_driver_init(void)
{
    int status = pci_register_driver(&my_driver);
    if (status < 0)
    {
        pr_err("CRYPTOCARD_MOD ---> cannot register driver, status %d\n", status);
    }
    status = setup_sysfs();
    if (status < 0)
    {
        pr_err("CRYPTOCARD_MOD ---> error creating sysfs, status %d\n", status);
    }
    status = setup_char_device();
    if (status < 0)
    {
        pr_err("CRYPTOCARD_MOD ---> error creating char device, status %d\n", status);
    }
    return 0;
}

static void __exit mypci_driver_exit(void)
{
    cleanup();
    pci_unregister_driver(&my_driver);
}

static void cleanup(void)
{
    kobject_put(kobj_ref);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&cryptcard_cdev);
    unregister_chrdev_region(dev, 1);
}

/* This function is called by the kernel */
static int my_driver_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int status = setup_device(pdev);
    if (status < 0)
    {
        pr_err("CRYPTOCARD_MOD ---> driver error, status %d\n", status);
    }
    return 0;
}

/* Clean up */
static void my_driver_remove(struct pci_dev *pdev)
{
    dma_free_coherent(&pdev->dev, 1000000, dma_buf, dma_handle);
    release_device(pdev);
    pr_info("CRYPTOCARD_MOD ---> Removed: Device vid: 0x%X pid: 0x%X\n", vendor, device);
}

static ssize_t sysfs_show_tid(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", TID);
}

static ssize_t sysfs_store_tid(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int val;
    kstrtoint(buf,10,&val);
    if(val<0) {
        pr_info("CRYPTOCARD_MOD ---> unlocking device %d\n", TID);
        TID = -1;
        mutex_unlock(&dev_lock);
    } else {
        mutex_lock(&dev_lock);
        TID = val;
        pr_info("CRYPTOCARD_MOD ---> locking device %d\n", TID);
    }
    return count;
}

static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    pr_info("CRYPTOCARD_MOD ---> sysfs_show %s\n", attr->attr.name);
    u8 val;
    if (strcmp(attr->attr.name, "DMA") == 0)
        val = DMA;
    else if (strcmp(attr->attr.name, "INTERRUPT") == 0)
        val = INTERRUPT;
    else if (strcmp(attr->attr.name, "KEY_A") == 0)
        val = KEY_A;
    else if (strcmp(attr->attr.name, "KEY_B") == 0)
        val = KEY_B;
    else if (strcmp(attr->attr.name, "DECRYPT") == 0)
        val = DECRYPT;
    else if (strcmp(attr->attr.name, "IS_MAPPED") == 0)
        val = IS_MAPPED;
    return sprintf(buf, "%d\n", val);
}

static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    u8 val = *buf;
    pr_info("CRYPTOCARD_MOD ---> sysfs_store %s %hhu\n", attr->attr.name, val);
    if((strcmp(attr->attr.name, "KEY_A") == 0) || (strcmp(attr->attr.name, "KEY_B") == 0))
    {
        if (strcmp(attr->attr.name, "KEY_A") == 0)
        {
            KEY_A = val;
            set_key();
        }
        else
        {
            KEY_B = val;
            set_key();
        }
    } else {
        if (!(val == 0 || val == 1))
        {
            pr_err("CRYPTOCARD_MOD ---> invalid value, can only be 0 or 1, recieved %hhu\n", val);
            return -EINVAL;
        }
        if (strcmp(attr->attr.name, "DMA") == 0)
            DMA = val;
        else if(strcmp(attr->attr.name, "INTERRUPT") == 0)
            INTERRUPT = val;
        else if(strcmp(attr->attr.name, "DECRYPT") == 0)
            DECRYPT = val;
        else if(strcmp(attr->attr.name, "IS_MAPPED") == 0)
            IS_MAPPED = val;
    }
    return count;
}

static int cdev_open(struct inode *inode, struct file *file)
{
    pr_info("CRYPTOCARD_MOD ---> Device File Opened\n");
    return 0;
}

/*
** This function will be called when we close the Device file
*/
static int cdev_release(struct inode *inode, struct file *file)
{
    pr_info("CRYPTOCARD_MOD ---> Device File Closed\n");
    return 0;
}

/*
** This function will be called when we read the Device file
*/
static ssize_t cdev_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    char *kbuf = (DMA) ? dma_buf : mmio_buf;
    if (IS_MAPPED || (copy_to_user(buf, kbuf, len) == 0))
    {
        pr_info("CRYPTOCARD_MOD ---> Read: %s\n", (IS_MAPPED ? NA : kbuf));
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
    char *kbuf = (DMA) ? dma_buf : mmio_buf;
    if (IS_MAPPED || (copy_from_user(kbuf, buf, klen) == 0))
    {
        pr_info("CRYPTOCARD_MOD ---> Write: %s\n", (IS_MAPPED ? NA : kbuf));
        is_data_ready=0;
        if(DMA) dma();
        else mmio();
        return klen;
    }
    return -1;
}

static int setup_char_device(void)
{
    if ((alloc_chrdev_region(&dev, 0, 1, "cryptcard_dev")) < 0)
    {
        pr_err("CRYPTOCARD_MOD ---> Cannot allocate major number for device\n");
        return -1;
    }
    pr_info("CRYPTOCARD_MOD ---> Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));
    cdev_init(&cryptcard_cdev, &fops);
    if ((cdev_add(&cryptcard_cdev, dev, 1)) < 0)
    {
        pr_err("CRYPTOCARD_MOD ---> Cannot add the device to the system\n");
        goto r_class;
    }
    if ((dev_class = class_create(THIS_MODULE, "cryptcard_class")) == NULL)
    {
        pr_err("CRYPTOCARD_MOD ---> Cannot create the struct class for device\n");
        goto r_class;
    }
    if ((device_create(dev_class, NULL, dev, NULL, "cryptcard_device")) == NULL)
    {
        pr_err("CRYPTOCARD_MOD ---> Cannot create the Device\n");
        goto r_device;
    }
    pr_info("CRYPTOCARD_MOD ---> Kernel Module Inserted Successfully\n");
    return 0;
r_device:
    class_destroy(dev_class);
r_class:
    unregister_chrdev_region(dev, 1);
    return -1;
}

static void set_key(void)
{
    iowrite32((KEY_A << 8) | KEY_B, MEM + 0x8);
}

static void write_to_device(void)
{
    int i;
    for (i = 0; i < ((klen + 4) / 4); i++)
    {
        u32 *cur = (u32 *)mmio_buf;
        u32 pos = OFFSET_DATA + i * 4;
        cur += i;
        iowrite32(*cur, MEM + pos);
    }
    pr_info("CRYPTOCARD_MOD ---> Write to device %s\n", mmio_buf);
}

static void read_from_device(void)
{   
    int i;
    for (i = 0; i < ((klen + 4) / 4); i++)
    {
        u32 pos = OFFSET_DATA + i * 4;
        u32 val = ioread32(MEM + pos);
        u32 *cur = (u32 *)mmio_buf;
        cur += i;
        *cur = val;
    }
    pr_info("CRYPTOCARD_MOD ---> READ from device %s\n", mmio_buf);
}

static void dma(void)
{
    iowrite32(klen, MEM + OFFSET_DMA_LENGTH);
    u32 status = 0x1;
    if(DECRYPT) status |= 0x2;
    if (INTERRUPT) {
        status |= 0x4;
    }
    pr_info("CRYPTOCARD_MOD ---> writing DMA command register 0x%x", status);
    iowrite32(dma_handle, MEM + OFFSET_DMA_DATA_ADDRESS);
    iowrite32(status, MEM + OFFSET_DMA_COMMAND);
    if(INTERRUPT) {
        pr_info("CRYPTOCARD_MOD ---> DMA with INTERRUPT");
        wait_event_interruptible(wq_is_data_ready,is_data_ready==1);
    } else 
    {
        pr_info("CRYPTOCARD_MOD ---> DMA w/o INTERRUPT");
        u32 val;
        do
        {
            val = ioread32(MEM + OFFSET_DMA_COMMAND);
        } while ((val&1) == 1);
        is_data_ready = 1;
    }
}

static void mmio(void)
{
    char *b1 = kmalloc(klen+5,GFP_KERNEL);
    int i;
    for (i = 0; i < ((klen + 4) / 4); i++)
    {
        u32 pos = OFFSET_DATA + i * 4;
        u32 val = ioread32(MEM + pos);
        u32 *cur = (u32 *)b1;
        cur += i;
        *cur = val;
    }
    pr_info("CRYPTOCARD_MOD ---> mmio klen %llu\n", klen);
    iowrite32(klen, MEM + OFFSET_MMIO_LENGTH);
    if(!IS_MAPPED) write_to_device();
    u32 status = 0x0;
    if(DECRYPT) status |= 0x02;
    if (INTERRUPT) {
        status |= 0x80;
    }
    pr_info("CRYPTOCARD_MOD ---> writing mmio status register 0x%x\n", status);
    iowrite32(status, MEM + OFFSET_MMIO_STATUS);
    iowrite32(OFFSET_DATA, MEM + OFFSET_MMIO_DATA_ADDRESS);
    if(INTERRUPT) {
        pr_info("CRYPTOCARD_MOD ---> MMIO ( mapped = %d ) with INTERRUPT\n", IS_MAPPED);
        wait_event_interruptible(wq_is_data_ready,is_data_ready==1);
    } else 
    {
        pr_info("CRYPTOCARD_MOD ---> MMIO ( mapped = %d ) w/o INTERRUPT\n", IS_MAPPED);
        u32 val;
        do
        {
            val = ioread32(MEM + OFFSET_MMIO_STATUS);
        } while ((val&1) == 1);
        if(!IS_MAPPED) read_from_device();
        is_data_ready=1;
    } 
}

static irqreturn_t irq_handler(int irq, void *cookie)
{

    u32 val = ioread32(MEM + OFFSET_INTERRUPT_STATUS);
    if (val == INTERRUPT_MMIO)
    {
        iowrite32(val, MEM + OFFSET_INTERRUPT_ACK);
        pr_info("CRYPTOCARD_MOD ---> IRQ mmio\n");
        if(!IS_MAPPED) read_from_device();
        is_data_ready=1;
        wake_up_interruptible(&wq_is_data_ready);
        return IRQ_HANDLED;
    } else if (val == INTERRUPT_DMA)
    {
        iowrite32(val, MEM + OFFSET_INTERRUPT_ACK);
        pr_info("CRYPTOCARD_MOD ---> IRQ dma\n");
        is_data_ready = 1;
        wake_up_interruptible(&wq_is_data_ready);
        return IRQ_HANDLED;
    }
    return IRQ_NONE;
}

/* Reqest interrupt and setup handler */
static int set_interrupts(struct pci_dev *pdev)
{
    int status = request_irq(IRQ_NO, irq_handler, IRQF_SHARED, "cryptocard_device", (void *)(irq_handler));
    if (status < 0)
    {
        pr_err("CRYPTOCARD_MOD ---> cannot register IRQ");
        free_irq(IRQ_NO, (void *)(irq_handler));
    }
    return status;
}

static int cdev_mmap(struct file *filp, struct vm_area_struct *vma )
{
	printk( "mmap: vm_start: 0x%lx, vm_end: 0x%lx, vm_pgoff: 0x%lx\n", vma->vm_start, vma->vm_end, vma->vm_pgoff );
	pr_info("%llu\n", PAGE_SHIFT);
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    pr_info("%llu\n", offset);
    offset += (mmio_start);
    pr_info("%llu\n", offset);
    offset = offset >> PAGE_SHIFT;
    pr_info("%llu\n", offset);
    pr_info("%llu\n", ( vma->vm_end - vma->vm_start));
    vma->vm_page_prot = pgprot_noncached( vma->vm_page_prot );
    int rc = io_remap_pfn_range( vma, vma->vm_start, offset, vma->vm_end - vma->vm_start, vma->vm_page_prot );
	if(rc)
	{
		pr_err("CRYPTOCARD_MOD ---> MPD_mmap: io_remap_pfn_range() error: rc = %d\n", rc );
        return -EAGAIN;
	}
    return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("whrugved");
MODULE_DESCRIPTION("CS730 CRYPTOCARD PCI driver");
MODULE_VERSION("0.1");

module_init(mypci_driver_init);
module_exit(mypci_driver_exit);