#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#define MY_DRIVER "my_pci_driver"
#define IDENTIFICATION 0x10C5730

u16 vendor, device;

/* This sample driver supports device with VID = 0x010F, and PID = 0x0F0E*/
static struct pci_device_id my_driver_id_table[] = {
    {PCI_DEVICE(0x1234, 0xdeba)},
    {
        0,
    }};

MODULE_DEVICE_TABLE(pci, my_driver_id_table);

static int my_driver_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void my_driver_remove(struct pci_dev *pdev);
void release_device(struct pci_dev *pdev);
u8 __iomem* get_memory(struct pci_dev *pdev);
int check_identification(struct pci_dev *pdev);
int set_up_device(struct pci_dev *pdev);
static int __init mypci_driver_init(void);
static void __exit mypci_driver_exit(void);

/* Driver registration structure */
static struct pci_driver my_driver = {
    .name = MY_DRIVER,
    .id_table = my_driver_id_table,
    .probe = my_driver_probe,
    .remove = my_driver_remove};

/* This is a "private" data structure */
/* You can store there any data that should be passed between driver's functions */
struct my_driver_priv
{
    u8 __iomem *hwmem;
};

void release_device(struct pci_dev *pdev)
{
    pci_disable_device(pdev);
    /* Free memory region */
    pci_release_region(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
}

u8 __iomem* get_memory(struct pci_dev *pdev)
{
    struct my_driver_priv *drv_priv = (struct my_driver_priv *)pci_get_drvdata(pdev);
    if (!drv_priv) printk(KERN_ERR "drv_priv is null");
    return drv_priv->hwmem;
}

int check_identification(struct pci_dev *pdev)
{
    u8 __iomem *mem = get_memory(pdev);
    unsigned int identification = ioread32(mem);
    if (identification != IDENTIFICATION)
    {
        printk(KERN_ERR "invalid identification 0x%X", identification);
        return -1;
    }
    printk(KERN_INFO "identification verified 0x%X", identification);
    return 0;
}

// void check_liveness(struct pci_dev *pdev)
// {
//     u8 __iomem* mem =  get_memory();
//     unsigned int identification = ioread32(mem);
//     if(identification!=IDENTIFICATION) {
//         printk(KERN_ERR "invalid identification 0x%X", identification)
//     } else printk(KERN_INFO "identification verified 0x%X", identification);
// }

int set_up_device(struct pci_dev *pdev)
{
    int bar, err;
    unsigned long mmio_start, mmio_len;
    struct my_driver_priv *drv_priv;
    pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(pdev, PCI_DEVICE_ID, &device);
    printk(KERN_INFO "Registered: Device vid: 0x%X pid: 0x%X\n", vendor, device);
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
    printk(KERN_INFO "mmio_start %lu mmio_len %lu\n", mmio_start, mmio_len);
    printk(KERN_INFO "mmio_start 0x%lX mmio_len 0x%lX\n", mmio_start, mmio_len);
    drv_priv = kzalloc(sizeof(struct my_driver_priv), GFP_KERNEL);
    if (!drv_priv)
    {
        release_device(pdev);
        return -ENOMEM;
    }
    drv_priv->hwmem = ioremap(mmio_start, mmio_len);
    if (!drv_priv->hwmem)
    {
        release_device(pdev);
        return -EIO;
    }
    pci_set_drvdata(pdev, drv_priv);
    if (check_identification(pdev) < 0) return -1;
    return 0;
    // if(check_liveness(pdev)<0) return -1;
}

static int __init mypci_driver_init(void)
{
    /* Register new PCI driver */
    int status = pci_register_driver(&my_driver);
    if (status < 0)
    {
        printk(KERN_ERR "cannot register driver, status %d\n", status);
    }
    return 0;
}

static void __exit mypci_driver_exit(void)
{
    /* Unregister */
    pci_unregister_driver(&my_driver);
}

/* This function is called by the kernel */
static int my_driver_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int status = set_up_device(pdev);
    if (status < 0)
    {
        printk(KERN_ERR "driver error, status %d\n", status);
    }
    return 0;
}

/* Clean up */
static void my_driver_remove(struct pci_dev *pdev)
{
    release_device(pdev);
    printk(KERN_INFO "Removed: Device vid: 0x%X pid: 0x%X\n", vendor, device);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ruke");
MODULE_DESCRIPTION("Test PCI driver");
MODULE_VERSION("0.1");

module_init(mypci_driver_init);
module_exit(mypci_driver_exit);