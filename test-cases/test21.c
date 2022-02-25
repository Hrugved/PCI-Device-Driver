#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <crypter.h>
#include<pthread.h>

int main( int argc, char *argv[] )
{
  DEV_HANDLE cdev;
  char *msg = "Hello CS730!";
  KEY_COMP a=30, b=17;
  uint64_t size = strlen(msg);
  char *op_text = malloc(size);
  strcpy(op_text, msg);
  cdev = create_handle();

  printf("%ld\n", size);

  if(cdev == ERROR)
  {
    printf("Unable to create handle for device\n");
    exit(0);
  }

  set_config(cdev, DMA, atoi(argv[1]));
  set_config(cdev, INTERRUPT, atoi(argv[2]));

  if(set_key(cdev, a, b) == ERROR){
    printf("Unable to set key\n");
    exit(0);
  }

  // map_card(cdev,1000);


  char *actual_buff = map_card(cdev, size);
  if(actual_buff == NULL)
  {
      printf("Testcase failed\n");
      exit(0);
  }
  strncpy(actual_buff, msg, size);

  printf("Original Text: %s\n", msg);

  encrypt(cdev, actual_buff, size, atoi(argv[3]));
  encrypt(cdev, actual_buff, size, atoi(argv[3]));
  encrypt(cdev, actual_buff, size, atoi(argv[3]));
  encrypt(cdev, actual_buff, size, atoi(argv[3]));
  printf("Encrypted Text: %s\n", actual_buff);

  // decrypt(cdev, actual_buff, size, 0);
  // printf("Decrypted Text: %s\n", actual_buff);

  close_handle(cdev);
  return 0;
}