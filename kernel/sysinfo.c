#include "sysinfo.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_sysinfo(void)
{
  struct sysinfo i = {.freemem=kmemstat(), .nproc=statnproc()};

  // user virtual address
  uint64 iv;
  argaddr(0, &iv);

  struct proc *p = myproc();
  if (copyout(p->pagetable, iv, (char *)&i, sizeof(struct sysinfo)) < 0)
    return -1;

  return 0;
}