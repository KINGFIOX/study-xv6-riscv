#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int fetch_addr(uint64_t addr, uint64_t* ip)
{
    struct proc* p = my_proc();
    if (addr >= p->sz || addr + sizeof(uint64_t) > p->sz) { // both tests needed, in case of overflow
        return -1;
    }
    if (copyin(p->pagetable, (char*)ip, addr, sizeof(*ip)) != 0) {
        return -1;
    }
    return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int fetch_str(uint64_t addr, char* buf, int max)
{
    struct proc* p = my_proc();
    if (copyin_str(p->pagetable, buf, addr, max) < 0) {
        return -1;
    }
    return strlen(buf);
}

/// @brief raw 就是: uint64_t
/// @param n
/// @return
static uint64_t arg_raw(int n)
{
    struct proc* p = my_proc();
    switch (n) {
    case 0:
        return p->trap_frame->a0;
    case 1:
        return p->trap_frame->a1;
    case 2:
        return p->trap_frame->a2;
    case 3:
        return p->trap_frame->a3;
    case 4:
        return p->trap_frame->a4;
    case 5:
        return p->trap_frame->a5;
    default:
        panic("arg_raw");
        return -1;
    }
}

// Fetch the nth 32-bit system call argument.
void arg_int(int n, int* ip)
{
    *ip = arg_raw(n); // 这里会出现: 类型降低
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
void arg_addr(int n, uint64_t* ip)
{
    *ip = arg_raw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int arg_str(int n, char* buf, int max)
{
    uint64_t addr;
    arg_addr(n, &addr);
    return fetch_str(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64_t sys_fork(void);
extern uint64_t sys_exit(void);
extern uint64_t sys_wait(void);
extern uint64_t sys_pipe(void);
extern uint64_t sys_read(void);
extern uint64_t sys_kill(void);
extern uint64_t sys_exec(void);
extern uint64_t sys_fstat(void);
extern uint64_t sys_chdir(void);
extern uint64_t sys_dup(void);
extern uint64_t sys_getpid(void);
extern uint64_t sys_sbrk(void);
extern uint64_t sys_sleep(void);
extern uint64_t sys_uptime(void);
extern uint64_t sys_open(void);
extern uint64_t sys_write(void);
extern uint64_t sys_mknod(void);
extern uint64_t sys_unlink(void);
extern uint64_t sys_link(void);
extern uint64_t sys_mkdir(void);
extern uint64_t sys_close(void);

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.

/// @brief 函数指针的数组
static uint64_t (*syscalls[])(void) = {
    [SYS_fork] = sys_fork,
    [SYS_exit] = sys_exit,
    [SYS_wait] = sys_wait,
    [SYS_pipe] = sys_pipe,
    [SYS_read] = sys_read,
    [SYS_kill] = sys_kill,
    [SYS_exec] = sys_exec,
    [SYS_fstat] = sys_fstat,
    [SYS_chdir] = sys_chdir,
    [SYS_dup] = sys_dup,
    [SYS_getpid] = sys_getpid,
    [SYS_sbrk] = sys_sbrk,
    [SYS_sleep] = sys_sleep,
    [SYS_uptime] = sys_uptime,
    [SYS_open] = sys_open,
    [SYS_write] = sys_write,
    [SYS_mknod] = sys_mknod,
    [SYS_unlink] = sys_unlink,
    [SYS_link] = sys_link,
    [SYS_mkdir] = sys_mkdir,
    [SYS_close] = sys_close,
};

void syscall(void)
{
    struct proc* p = my_proc();

    int num = p->trap_frame->a7;
    if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        // Use num to lookup the system call function for num, call it,
        // and store its return value in p->trap_frame->a0
        p->trap_frame->a0 = syscalls[num]();
    } else {
        printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
        p->trap_frame->a0 = -1;
    }
}
