#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
// uint64 sys_map_shared_pages(void) {
//     uint64 src_va;
//     int pid;
//     uint64 size;

//     argaddr(0, &src_va);
//      argint(1, &pid);
//     argaddr(2, &size);

//     struct proc* dst = find_proc_by_pid(pid); // you implement this helper
//     if (dst == 0) return -1;

//     return map_shared_pages(myproc(), dst, src_va, size);
// }


// Assume this is the syscall handler for: int map_shared_pages(void *addr, int pid, uint64 size);

uint64
sys_map_shared_pages(void) {
    uint64 parent_va;
    int parent_pid;
    uint64 size;

    argaddr(0, &parent_va); argint(1, &parent_pid) ; argaddr(2, &size);

    struct proc *child = myproc();
    struct proc *parent = find_proc_by_pid(parent_pid);
    if (!parent)
        return -1;

    // Align sizes
    uint64 rounded_size = PGROUNDUP(size);
    uint64 dst_va = PGROUNDUP(child->sz);  // child maps at end of its space

    for (uint64 off = 0; off < rounded_size; off += PGSIZE) {
        pte_t *ppte = walk(parent->pagetable, parent_va + off, 0);
        if (!ppte || !(*ppte & PTE_V) || !(*ppte & PTE_U))
            return -1;

        uint64 pa = PTE2PA(*ppte);
        int perm = (PTE_FLAGS(*ppte) & (PTE_R | PTE_W | PTE_X | PTE_U)) | PTE_S;

        if (mappages(child->pagetable, dst_va + off, PGSIZE, pa, perm) < 0)
            return -1;
    }

    child->sz = dst_va + rounded_size;  // update size
    return dst_va;  // return the address in child where it was mapped
}




uint64 sys_unmap_shared_pages(void) {
    uint64 addr, size;
  argaddr(0, &addr); argaddr(1, &size);
        
    return unmap_shared_pages(myproc(), addr, size);
}

