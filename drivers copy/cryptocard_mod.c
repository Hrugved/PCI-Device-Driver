#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/sched.h>

int init_module(void)
{
	printk(KERN_INFO "Hello kernel\n");
	return 0;
}

void cleanup_module(void)
{
	printk(KERN_INFO "Goodbye kernel\n");
}

MODULE_DESCRIPTION("My kernel module");
MODULE_AUTHOR("arunkp1986@gmail.com");
MODULE_LICENSE("GPL");
  