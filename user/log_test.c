#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define MAX_CHILDREN 4

// Shared memory pointers
volatile uint32 *header;
char *buffer;
uint64 shared_addr;

char *short_msg = "hello world";
char *long_msg =
  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

void write_log(int child_pid, char *msg, int len) {
  int offset = 0;

  while (offset + 4 + len <= PGSIZE) {
    offset = (offset + 3) & ~3; // align to 4 bytes

    volatile uint32 *hdr = (uint32 *)(buffer + offset);

    if (__sync_val_compare_and_swap(hdr, 0, ((child_pid << 16) | len)) == 0) {
      memmove((void *)(hdr + 1), msg, len);
      return;
    }

    int entry_len = (*(uint32 *)hdr) & 0xFFFF;
    offset += 4 + entry_len;
  }

  // No space
  exit(0);
}

void parent_read_logs() {
  int offset = 0;
  int total = 0;

  while (offset + 4 < PGSIZE) {
    offset = (offset + 3) & ~3; // align to 4 bytes
    uint32 hdr = *(uint32 *)(buffer + offset);
    if (hdr == 0)
      break;

    int pid = hdr >> 16;
    int len = hdr & 0xFFFF;

    offset += 4;
    if (offset + len > PGSIZE) break;

    printf("Parent: Message from child with PID %d: ", pid);
    write(1, buffer + offset, len);
    printf(", with offset %d\n", offset - 4);

    offset += len;
    total++;
  }

  printf("Parent: Total messages read: %d\n", total);
}

int main(int argc, char *argv[]) {
  printf("log_test\n");

  buffer = malloc(PGSIZE);
  if (!buffer) {
    printf("Failed to allocate buffer\n");
    exit(1);
  }

  shared_addr = (uint64)buffer;
  int parent_pid = getpid();

  printf("Parent: malloced buffer at 0x%p\n", buffer);
  printf("Parent: PID is %d\n", parent_pid);

  for (int i = 0; i < MAX_CHILDREN; i++) {
    int pid = fork();
    if (pid == 0) {
      uint64 mapped = map_shared_pages((void *)shared_addr, parent_pid, PGSIZE);
      if (mapped == 0) {
        printf("Child %d: map_shared_pages failed\n", getpid());
        exit(1);
      }

      buffer = (char *)mapped;
      int my_pid = getpid();
      char *msg;
      int len;

      sleep(5);

      if (i == 1) {
        msg = long_msg;
        len = strlen(msg);
        for (int j = 0; j < 1; j++) // long message is big
          write_log(my_pid, msg, len);
      } else {
        msg = short_msg;
        len = strlen(msg);
        for (int j = 0; j < 30; j++)
          write_log(my_pid, msg, len);
      }

      exit(0);
    } else {
      printf("Parent: Child %d has PID %d\n", i, pid);
    }
  }

  for (int i = 0; i < MAX_CHILDREN; i++) {
    wait(0);
  }

  parent_read_logs();
  exit(0);
}
