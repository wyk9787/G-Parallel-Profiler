#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "inspect.h"

int main(int argc, char** argv) {
  if(argc != 3) {
    fprintf(stderr, "Usage: %s <pid> <address>\n", argv[0]);
    exit(1);
  }

  pid_t pid;
  void* addr;
  sscanf(argv[1], "%d", &pid);
  sscanf(argv[2], "%p", &addr);

  const char* fname = address_to_function(pid, addr);
  if(fname == NULL) {
    fprintf(stderr, "Could not find function information.\n");
  } else {
    printf("%s\n", fname);
  }

  return 0;
}

