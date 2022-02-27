#include<crypter.h>
#include<stdio.h>
#include<sys/mman.h>
#include<unistd.h>
#include<pthread.h>

#define DMA_DATA_SIZE 32768
#define MMIO_DATA_SIZE 1048407
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static pthread_mutex_t mutex_list;
typedef struct dev_handle {
  int fd;
  char DMA;
  char INTERRUPT;
  char key_A;
  char key_B;
  uint64_t mmap_size; 
  struct dev_handle *next;
} dev_handle;

static dev_handle *head = NULL;

static void _add_handle(int fd) {
  pthread_mutex_lock(&mutex_list);
  dev_handle *handle = malloc(sizeof(handle));
  handle->fd=fd;
  handle->DMA=0;
  handle->INTERRUPT=0;
  handle->key_A=0;
  handle->key_B=0;
  handle->mmap_size=0;
  handle->next=head;
  head=handle;
  pthread_mutex_unlock(&mutex_list);
}

static void _remove_handle(int fd) {
  pthread_mutex_lock(&mutex_list);
  dev_handle *temp = head;
  if(head->fd==fd) {
    head=head->next;
  } else {
    while(temp != NULL) {
      if((temp->next!=NULL) && (temp->next->fd == fd)) {
        temp->next = temp->next->next;
        break;
      }
      temp = temp->next;
    }
  }
  pthread_mutex_unlock(&mutex_list); 
}

static dev_handle* _get_handle(int fd) {
  pthread_mutex_lock(&mutex_list);
  dev_handle *temp = head;
  while(temp != NULL) {
    if(temp->fd == fd) {    
      pthread_mutex_unlock(&mutex_list);
      return temp;
    }
    temp = temp->next;
  }
  pthread_mutex_unlock(&mutex_list);
  return NULL;
}

static void _lock_device() 
{
  int fd = open("/sys/kernel/cryptocard_sysfs/TID", O_WRONLY);
  int tid = gettid();
  char buf[10];
  int data_len = snprintf(buf, sizeof(buf), "%d\n", tid);
  write (fd, buf, data_len);
  close(fd);
}

static void _unlock_device() 
{
  int fd = open("/sys/kernel/cryptocard_sysfs/TID", O_WRONLY);
  int tid = -1;
  char buf[10];
  int data_len = snprintf(buf, sizeof(buf), "%d\n", tid);
  write (fd, buf, data_len);
  close(fd);
}

static void _set_device_decrypt(uint8_t value) {
  int fd = open("/sys/kernel/cryptocard_sysfs/DECRYPT", O_WRONLY);
  write (fd, (const char *)&value, 1);
  close(fd);
}

static void _set_device_is_mapped(uint8_t value) {
  int fd = open("/sys/kernel/cryptocard_sysfs/IS_MAPPED", O_WRONLY);
  write (fd, (const char *)&value, 1);
  close(fd);
}

static void _set_device_dma(uint8_t value) {
  int fd = open("/sys/kernel/cryptocard_sysfs/DMA", O_WRONLY);
  write (fd, (const char *)&value, 1);
  close(fd);
  return 0;
}

static void _set_device_interrupt(uint8_t value) {
  int fd = open("/sys/kernel/cryptocard_sysfs/INTERRUPT", O_WRONLY);
  write (fd, (const char *)&value, 1);
  close(fd);
  return 0;
}

static void _set_device_key_A(uint8_t value) {
  int fd = open("/sys/kernel/cryptocard_sysfs/KEY_A", O_WRONLY);
  write (fd, (const char *)&value, 1);
  close(fd);
  return 0;
}

static void _set_device_Key_B(uint8_t value) {
  int fd = open("/sys/kernel/cryptocard_sysfs/KEY_B", O_WRONLY);
  write (fd, (const char *)&value, 1);
  close(fd);
  return 0;
}

static uint8_t _set_device_config(DEV_HANDLE cdev, uint8_t isMapped, uint8_t isDecrypt) {
  dev_handle *handle = _get_handle(cdev);
  _set_device_is_mapped(isMapped);
  _set_device_decrypt(isDecrypt);
  _set_device_dma(handle->DMA);
  _set_device_interrupt(handle->INTERRUPT);
  _set_device_key_A(handle->key_A);
  _set_device_Key_B(handle->key_B);
  return handle->DMA;
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
  _add_handle(fd);
  return fd; 
}

/*Function template to close device handle.
Takes an already opened device handle as an arguments*/
void close_handle(DEV_HANDLE cdev)
{
  _remove_handle(cdev);
  close(cdev);
}

void _device_operate(DEV_HANDLE cdev, ADDR_PTR addr, uint64_t length) {
  if(write(cdev, addr, length) < 0){
    perror("write");
    exit(-1);
  }
  if(read(cdev, addr, length) < 0){
    perror("read");
    exit(-1);
  }
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
  _lock_device();
  uint8_t DMA = _set_device_config(cdev,isMapped,UNSET);
  uint64_t chunk_size = (DMA) ? DMA_DATA_SIZE : MMIO_DATA_SIZE;
  int i;
  for (i = 0; i < ((length + chunk_size - 1) / chunk_size); i++)
  {
      _device_operate(cdev,  addr + (i * chunk_size), MIN(chunk_size,length-(i*chunk_size)));
  }
  _unlock_device();
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
  _lock_device();
  uint8_t DMA = _set_device_config(cdev,isMapped,SET);
  uint64_t chunk_size = (DMA) ? DMA_DATA_SIZE : MMIO_DATA_SIZE;
  int i;
  for (i = 0; i < ((length + chunk_size - 1) / chunk_size); i++)
  {
      _device_operate(cdev,  addr + (i * chunk_size), MIN(chunk_size,length-(i*chunk_size)));
  }
  _unlock_device();
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
  dev_handle *handle = _get_handle(cdev);
  handle->key_A = a; 
  handle->key_B = b;
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
  dev_handle *handle = _get_handle(cdev);
  if(type==DMA) {
    handle->DMA = value; 
  } else {
    handle->INTERRUPT = value;
  }
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
  dev_handle *handle = _get_handle(cdev);
  handle->mmap_size=size;
  ptr = ptr + DEVICE_MEMORY_OFFSET;
  return ptr;
}

/*Function template to device input/output memory into user space.
Takes three arguments
  cdev: opened device handle
  addr: memory-mapped address to unmap from user-space*/
void unmap_card(DEV_HANDLE cdev, ADDR_PTR addr)
{
  dev_handle *handle = _get_handle(cdev);
  addr = addr - DEVICE_MEMORY_OFFSET;
  munmap(addr,handle->mmap_size);
}