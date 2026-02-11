#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define SHM_SIZE 4096

int
main(int argc, char *argv[])
{
  int disable_unmap = (argc > 1 && strcmp(argv[1], "--no-unmap") == 0);

  // Parent mallocs one page and writes message
  char *buf = malloc(SHM_SIZE);
  if (!buf) {
    printf("Parent: malloc failed\n");
    exit(1);
  }
  strcpy(buf, "Hello daddy\n");
  printf("Parent: malloced buffer at %p\n", buf);

  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // --- Child ---
    printf("Child: sz before    = %p\n", sbrk(0));
    sleep(1);
    printf("Child: sz after map = %p\n", sbrk(0));
    printf("Child sees: %s", buf);
    if (!disable_unmap) {
      int ur = unmap_shared_pages(buf, SHM_SIZE);
      printf("Child: unmap returned = %d\n", ur);
    } else {
      printf("Child: skipping unmap\n");
    }
    printf("Child: sz after unmap = %p\n", sbrk(0));
    void *m = malloc(SHM_SIZE);
    printf("Child: sz after malloc = %p\n", sbrk(0));
    (void)m;
    exit(0);
  }

  // --- Parent ---
  int r = map_shared_pages(buf, pid, SHM_SIZE);
  // DO NOT PRINT HERE!
  wait(0);  // Wait for child

  // Now print parent result *after* child output is done
  printf("Parent: map_shared_pages returned %d\n", r);
  printf("Parent: final content = \"%s\"\n", buf);
  if (!disable_unmap) {
    int ur = unmap_shared_pages(buf, SHM_SIZE);
    printf("Parent: unmap_shared_pages returned %d, heap is %p\n", ur, sbrk(0));
  } else {
    printf("Parent: skipping unmap\n");
  }
  free(buf);
  exit(0);
}
