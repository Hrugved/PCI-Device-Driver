#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <crypter.h>
#include <unistd.h>

DEV_HANDLE cdev1,cdev2,cdev3,cdev4;
char *op_text;
uint64_t size;

void* f1(void* arg) {
  _lock_device();
  printf("locked %d\n", gettid());
  sleep(5);
  _unlock_device();
  printf("unlocked %d\n", gettid());
  pthread_exit(0);
}

void* f2(void* arg) {
  _lock_device();
  printf("locked %d\n", gettid());
  sleep(2);
  _unlock_device();
  printf("unlocked %d\n", gettid());
  pthread_exit(0);
}

int main( int argc, char *argv[] )
{
  // char *msg = "Hello CS730!";
  // KEY_COMP a=30, b=17;
  // size = strlen(msg);
  // op_text = malloc(size);
  // strcpy(op_text, msg);
  // cdev1 = create_handle();
  // cdev2 = create_handle();
  // cdev3 = create_handle();
  // cdev4 = create_handle();

  // printf("%ld\n", size);

  // if(cdev == ERROR)
  // {
  //   printf("Unable to create handle for device\n");
  //   exit(0);
  // }

  // set_config(cdev, DMA, atoi(argv[1]));
  // set_config(cdev, INTERRUPT, atoi(argv[2]));

  // if(set_key(cdev, a, b) == ERROR){
  //   printf("Unable to set key\n");
  //   exit(0);
  // }

  // printf("Original Text: %s\n", msg);

  // encrypt(cdev, op_text, size, atoi(argv[3]));

  pthread_t t1,t2;
  pthread_create(&t1, NULL, f1, NULL);
  pthread_create(&t2, NULL, f2, NULL);
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  // printf("Encrypted Text: %s\n", actual_buff);

  // decrypt(cdev, actual_buff, size, 0);
  // printf("Decrypted Text: %s\n", actual_buff);

  // close_handle(cdev1);
  // close_handle(cdev2);
  // close_handle(cdev3);
  // close_handle(cdev4);
  return 0;
}
