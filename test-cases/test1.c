#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <crypter.h>

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

  printf("Original Text: %s\n", msg);

  encrypt(cdev, op_text, size, 0);
  printf("Encrypted Text: %s\n", op_text);

  decrypt(cdev, op_text, size, 0);
  printf("Decrypted Text: %s\n", op_text);

  close_handle(cdev);
  return 0;
}
