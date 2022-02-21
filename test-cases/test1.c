#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <crypter.h>

int main()
{
  DEV_HANDLE cdev;
  char *msg = "Hello CS730! is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum.";
  KEY_COMP a=30, b=17;
  uint64_t size = strlen(msg);
  char *op_text = malloc(size);
  strcpy(op_text, msg);
  cdev = create_handle();

  printf("%lld\n", size);

  if(cdev == ERROR)
  {
    printf("Unable to create handle for device\n");
    exit(0);
  }

  set_config(cdev, INTERRUPT, 1);

  if(set_key(cdev, a, b) == ERROR){
    printf("Unable to set key\n");
    exit(0);
  }

  printf("Original Text: %s\n", msg);

  encrypt(cdev, op_text, size, 0);
  printf("Encrypted Text: %s\n", op_text);

  // decrypt(cdev, op_text, size, 0);
  // printf("Decrypted Text: %s\n", op_text);


  // set_key(cdev,3,245);

  close_handle(cdev);
  return 0;
}
