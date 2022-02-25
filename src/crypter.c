#include<crypter.h>
#include<stdio.h>
#include<sys/mman.h>
#include<unistd.h>

void _lock_device() 
{
  int fd = open("/sys/kernel/cryptocard_sysfs/TID", O_WRONLY);
  int tid = gettid();
  char buf[10];
  int data_len = snprintf(buf, sizeof(buf), "%d\n", tid);
  write (fd, buf, data_len);
  close(fd);
}

void _unlock_device() 
{
  int fd = open("/sys/kernel/cryptocard_sysfs/TID", O_WRONLY);
  int tid = -1;
  char buf[10];
  int data_len = snprintf(buf, sizeof(buf), "%d\n", tid);
  write (fd, buf, data_len);
  close(fd);
}

static void _set_decrypt(uint8_t value) {
  int fd = open("/sys/kernel/cryptocard_sysfs/DECRYPT", O_WRONLY);
  write (fd, (const char *)&value, 1);
  close(fd);
}

static void _set_is_mapped(uint8_t value) {
  int fd = open("/sys/kernel/cryptocard_sysfs/IS_MAPPED", O_WRONLY);
  write (fd, (const char *)&value, 1);
  close(fd);
}

/*Function template to create handle for the CryptoCard device.
On success it returns the device handle as an integer*/
DEV_HANDLE create_handle()
{ 
  DEV_HANDLE fd = open("/dev/cryptcard_device",O_RDWR);
  if(fd < 0){
      perror("open");
      exit(-1);
  }
  return fd; 
}

/*Function template to close device handle.
Takes an already opened device handle as an arguments*/
void close_handle(DEV_HANDLE cdev)
{
  close(cdev);
}

/*Function template to encrypt a message using MMIO/DMA/Memory-mapped.
Takes four arguments
  cdev: opened device handle
  addr: data address on which encryption has to be performed
  length: size of data to be encrypt
  isMapped: TRUE if addr is memory-mapped address otherwise FALSE
*/
int encrypt(DEV_HANDLE cdev, ADDR_PTR addr, uint64_t length, uint8_t isMapped)
{
  _set_decrypt(0);
  _set_is_mapped(isMapped);
  if(write(cdev, addr, length) < 0){
    perror("write");
    exit(-1);
  }
  // if(read(cdev, addr, length) < 0){
  //   perror("read");
  //   exit(-1);
  // }
  return 0;
}

/*Function template to decrypt a message using MMIO/DMA/Memory-mapped.
Takes four arguments
  cdev: opened device handle
  addr: data address on which decryption has to be performed
  length: size of data to be decrypt
  isMapped: TRUE if addr is memory-mapped address otherwise FALSE
*/
int decrypt(DEV_HANDLE cdev, ADDR_PTR addr, uint64_t length, uint8_t isMapped)
{
  _set_decrypt(1);
  _set_is_mapped(isMapped);
  if(write(cdev, addr, length) < 0){
    perror("write");
    exit(-1);
   }
  if(read(cdev, addr, length) < 0){
    perror("read");
    exit(-1);
  }
  return 0;
}

/*Function template to set the key pair.
Takes three arguments
  cdev: opened device handle
  a: value of key component a
  b: value of key component b
Return 0 in case of key is set successfully*/
int set_key(DEV_HANDLE cdev, KEY_COMP a, KEY_COMP b)
{
  int fd;
  fd = open("/sys/kernel/cryptocard_sysfs/KEY_A", O_WRONLY);
  write (fd, (const char *)&a, 1);
  close(fd);
  fd = open("/sys/kernel/cryptocard_sysfs/KEY_B", O_WRONLY);
  write (fd, (const char *)&b, 1);
  close(fd);
  return 0;
}

/*Function template to set configuration of the device to operate.
Takes three arguments
  cdev: opened device handle
  type: type of configuration, i.e. set/unset DMA operation, interrupt
  value: SET/UNSET to enable or disable configuration as described in type
Return 0 in case of key is set successfully*/
int set_config(DEV_HANDLE cdev, config_t type, uint8_t value)
{
  int fd;
  if(type==DMA) {
    fd = open("/sys/kernel/cryptocard_sysfs/DMA", O_WRONLY);
  } else {
    fd = open("/sys/kernel/cryptocard_sysfs/INTERRUPT", O_WRONLY);
  }
  write (fd, (const char *)&value, 1);
  close(fd);
  return 0;
}

/*Function template to device input/output memory into user space.
Takes three arguments
  cdev: opened device handle
  size: amount of memory-mapped into user-space (not more than 1MB strict check)
Return virtual address of the mapped memory*/
ADDR_PTR map_card(DEV_HANDLE cdev, uint64_t size)
{
  ADDR_PTR ptr = mmap(NULL,size,PROT_WRITE|PROT_READ,MAP_SHARED,cdev,0);
  if (ptr == MAP_FAILED) {
      printf("mmap failed\n");
      return NULL;
  }
  ptr = ptr + 0xa8;
  return ptr;
}

/*Function template to device input/output memory into user space.
Takes three arguments
  cdev: opened device handle
  addr: memory-mapped address to unmap from user-space*/
void unmap_card(DEV_HANDLE cdev, ADDR_PTR addr)
{

}