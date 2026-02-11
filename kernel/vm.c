#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

pagetable_t kernel_pagetable;
extern char etext[];
extern char trampoline[];

pagetable_t kvmmake(void) {
  pagetable_t kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R|PTE_W);
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R|PTE_W);
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R|PTE_W);
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R|PTE_X);
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R|PTE_W);
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R|PTE_X);
  proc_mapstacks(kpgtbl);
  return kpgtbl;
}

void kvminit(void) {
  kernel_pagetable = kvmmake();
}

void kvminithart(void) {
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

pte_t* walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA) panic("walk: va >= MAXVA");
  for (int lvl = 2; lvl > 0; lvl--) {
    pte_t *pte = &pagetable[PX(lvl, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pde_t*)kalloc()) == 0) return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  if (va >= MAXVA) return 0;
  pte_t *pte = walk(pagetable, va, 0);
  if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) return 0;
  return PTE2PA(*pte);
}

void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(kpgtbl, va, sz, pa, perm) < 0) panic("kvmmap");
}

int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
  uint64 a = PGROUNDDOWN(va);
  uint64 last = PGROUNDDOWN(va + size - 1);
  for (;;) {
    pte_t *pte = walk(pagetable, a, 1);
    if (!pte) return -1;
    if (*pte & PTE_V) {
      printf("mappages: remap at VA 0x%p\n", a);
      panic("mappages: remap");
    }
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last) break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  if (va % PGSIZE) panic("uvmunmap: not aligned");
  for (uint64 a = va; a < va + npages*PGSIZE; a += PGSIZE) {
    pte_t *pte = walk(pagetable, a, 0);
    if (!pte || !(*pte & PTE_V)) panic("uvmunmap: not mapped");
    if (!(PTE_FLAGS(*pte) & PTE_V)) panic("uvmunmap: not a leaf");
    if (do_free && !(*pte & PTE_S)) kfree((void*)PTE2PA(*pte));
    *pte = 0;
  }
}

pagetable_t uvmcreate(void) {
  pagetable_t p = (pagetable_t)kalloc();
  if (!p) return 0;
  memset(p, 0, PGSIZE);
  return p;
}

void uvmfirst(pagetable_t pagetable, uchar *src, uint sz) {
  if (sz >= PGSIZE) panic("uvmfirst: more than a page");
  char *mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int permflag) {
  if (newsz < oldsz) return oldsz;
  uint64 a = PGROUNDUP(oldsz);
  for (; a < newsz; a += PGSIZE) {
    char *mem = kalloc();
    if (!mem) { uvmdealloc(pagetable, a, oldsz); return 0; }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|permflag) < 0) {
      kfree(mem); uvmdealloc(pagetable, a, oldsz); return 0;
    }
  }
  return newsz;
}

uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;
  uint64 oldpg = PGROUNDUP(oldsz), newpg = PGROUNDUP(newsz);
  if (newpg < oldpg) uvmunmap(pagetable, newpg, (oldpg-newpg)/PGSIZE, 1);
  return newsz;
}

void freewalk(pagetable_t pagetable) {
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && !(pte & (PTE_R|PTE_W|PTE_X))) {
      freewalk((pagetable_t)PTE2PA(pte));
      pagetable[i] = 0;
    } else if (pte & PTE_V) panic("freewalk: leaf");
  }
  kfree((void*)pagetable);
}

void uvmfree(pagetable_t pagetable, uint64 sz) {
  if (sz) uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  uint64 i;
  for (i = 0; i < sz; i += PGSIZE) {
    pte_t *pte = walk(old, i, 0);
    if (!pte || !(*pte & PTE_V)) panic("uvmcopy: pte should exist");
    uint64 pa = PTE2PA(*pte);
    int flags = PTE_FLAGS(*pte);
    char *mem = kalloc();
    if (!mem) goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) < 0) { kfree(mem); goto err; }
  }
  return 0;
err:
  uvmunmap(new, 0, (i/PGSIZE), 1);
  return -1;
}

void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0);
  if (!pte) panic("uvmclear");
  *pte &= ~PTE_U;
}

int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  while (len) {
    uint64 va0 = PGROUNDDOWN(dstva), pa0 = walkaddr(pagetable, va0);
    if (!pa0) return -1;
    uint64 n = PGSIZE - (dstva - va0);
    if (n > len) n = len;
    memmove((void*)(pa0 + (dstva - va0)), src, n);
    len -= n; src += n; dstva = va0 + PGSIZE;
  }
  return 0;
}

int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  while (len) {
    uint64 va0 = PGROUNDDOWN(srcva), pa0 = walkaddr(pagetable, va0);
    if (!pa0) return -1;
    uint64 n = PGSIZE - (srcva - va0);
    if (n > len) n = len;
    memmove(dst, (void*)(pa0 + (srcva - va0)), n);
    len -= n; dst += n; srcva = va0 + PGSIZE;
  }
  return 0;
}

int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  int got_null = 0;
  while (!got_null && max) {
    uint64 va0 = PGROUNDDOWN(srcva), pa0 = walkaddr(pagetable, va0);
    if (!pa0) return -1;
    uint64 n = PGSIZE - (srcva - va0); if (n > max) n = max;
    char *p = (char*)(pa0 + (srcva - va0));
    while (n--) {
      if (*p == '\0') { *dst = '\0'; got_null = 1; break; }
      *dst = *p; p++; dst++; max--;
    }
    srcva = va0 + PGSIZE;
  }
  return got_null ? 0 : -1;
}

static uint64 find_free_va(pagetable_t pagetable, uint64 needed) {
  uint64 base  = PGROUNDUP(MAXVA/2);
  uint64 limit = MAXVA - needed;
  for (uint64 va = base; va < limit; va += PGSIZE) {
    int conflict = 0;
    for (uint64 ck = va; ck < va + needed; ck += PGSIZE) {
      pte_t *p = walk(pagetable, ck, 0);
      if (p && (*p & PTE_V)) { conflict = 1; break; }
    }
    if (!conflict) return va;
  }
  return 0;
}

uint64 map_shared_pages(struct proc *src, struct proc *dst, uint64 src_va, uint64 size) {
  if (!src || !dst || size == 0) return 0;

  uint64 start  = PGROUNDDOWN(src_va);
  uint64 end    = PGROUNDUP(src_va + size);
  uint64 length = end - start;

  uint64 dst_va = find_free_va(dst->pagetable, length);
  if (!dst_va) return 0;

  uint64 ret_va = dst_va + (src_va - start);

  for (uint64 a = start; a < end; a += PGSIZE) {
    pte_t *spte = walk(src->pagetable, a, 0);
    if (!spte || !(*spte & PTE_V) || !(*spte & PTE_U))
      return 0;

    uint64 pa = PTE2PA(*spte);
    int perm  = (PTE_FLAGS(*spte)&(PTE_R|PTE_W|PTE_X|PTE_U)) | PTE_S;

    pte_t *dpte = walk(dst->pagetable, dst_va, 0);
    if (dpte && (*dpte & PTE_V)) {
      printf("map_shared_pages: remap at dst_va 0x%p\n", dst_va);
      return 0;
    }

    if (mappages(dst->pagetable, dst_va, PGSIZE, pa, perm) < 0)
      return 0;

    dst_va += PGSIZE;
  }

  if (dst->sz < dst_va)
    dst->sz = dst_va;

  return ret_va;
}

uint64 unmap_shared_pages(struct proc *p, uint64 addr, uint64 size) {
  if (!p || size == 0) return -1;
  uint64 start = PGROUNDDOWN(addr), end = PGROUNDUP(addr + size);
  for (uint64 a = start; a < end; a += PGSIZE) {
    pte_t *pte = walk(p->pagetable, a, 0);
    if (!pte || !(*pte & PTE_V)) return -1;
    *pte &= ~PTE_U;
  }
  return 0;
}
