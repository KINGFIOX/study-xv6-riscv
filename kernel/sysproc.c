#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64_t
sys_exit(void)
{
    int n;
    arg_int(0, &n);
    exit(n);
    return 0; // not reached
}

uint64_t
sys_getpid(void)
{
    return my_proc()->pid;
}

uint64_t
sys_fork(void)
{
    return fork();
}

uint64_t
sys_wait(void)
{
    uint64_t p;
    arg_addr(0, &p);
    return wait(p);
}

uint64_t
sys_sbrk(void)
{
    uint64_t addr;
    int n;

    arg_int(0, &n);
    addr = my_proc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

uint64_t
sys_sleep(void)
{
    int n;
    uint_t ticks0;

    arg_int(0, &n);
    if (n < 0)
        n = 0;
    acquire(&ticks_lock);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (killed(my_proc())) {
            release(&ticks_lock);
            return -1;
        }
        sleep(&ticks, &ticks_lock);
    }
    release(&ticks_lock);
    return 0;
}

uint64_t
sys_kill(void)
{
    int pid;

    arg_int(0, &pid);
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64_t
sys_uptime(void)
{
    uint_t xticks;

    acquire(&ticks_lock);
    xticks = ticks;
    release(&ticks_lock);
    return xticks;
}
