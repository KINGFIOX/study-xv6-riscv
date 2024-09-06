#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

/**
 * @brief 进程表
 *
 */
struct proc procs[NPROC];

struct proc* init_proc;

int next_pid = 1;
struct spinlock pid_lock;

extern void fork_ret(void);
static void free_proc(struct proc* p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_map_stacks(pagetable_t kpgtbl)
{
    struct proc* p;

    for (p = procs /* 进程数组的首地址 */; p < &procs[NPROC]; p++) {
        char* pa = k_alloc(); // 进程的 stack
        if (pa == 0) {
            panic("k_alloc");
        }
        uint64_t va = KSTACK((int)(p - procs));
        k_vm_map(kpgtbl, va, (uint64_t)pa, PGSIZE, PTE_R | PTE_W);
    }
}

// initialize the proc table.
void proc_init(void)
{
    init_lock(&pid_lock, "next_pid");
    init_lock(&wait_lock, "wait_lock");
    for (struct proc* p = procs; p < &procs[NPROC] /* 遍历数组 */; p++) {
        init_lock(&p->lock, "proc");
        p->state = UNUSED;
        p->kstack = KSTACK((int)(p - procs));
    }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpu_id()
{
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu* my_cpu(void)
{
    int id = cpu_id();
    struct cpu* c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc* my_proc(void)
{
    push_off();
    struct cpu* c = my_cpu();
    struct proc* p = c->proc;
    pop_off();
    return p;
}

int alloc_pid()
{
    int pid;
    acquire(&pid_lock);
    pid = next_pid;
    next_pid = next_pid + 1;
    release(&pid_lock);
    return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc* alloc_proc(void)
{
    struct proc* p;
    for (p = procs; p < &procs[NPROC]; p++) {
        acquire(&p->lock); // 上锁, 看一下是不是 unused
        if (p->state == UNUSED) {
            goto found;
        } else {
            release(&p->lock);
        }
    }
    return 0;

found:
    p->pid = alloc_pid();
    p->state = USED;

    // Allocate a trap_frame page.
    if ((p->trap_frame = (struct trap_frame*)k_alloc()) == 0) {
        free_proc(p);
        release(&p->lock); // 这个锁是在 goto found 前 acquire 的
        return 0;
    }

    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0) {
        free_proc(p);
        release(&p->lock);
        return 0;
    }

    // Set up new context to start executing at fork_ret,
    // which returns to user space.
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64_t)fork_ret;
    p->context.sp = p->kstack + PGSIZE;

    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void free_proc(struct proc* p)
{
    if (p->trap_frame) {
        k_free((void*)p->trap_frame);
    }
    p->trap_frame = 0;
    if (p->pagetable) {
        proc_free_pagetable(p->pagetable, p->sz);
    }
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trap_frame pages.
pagetable_t proc_pagetable(struct proc* p)
{
    // An empty page table.
    pagetable_t pagetable = uvm_create();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (map_pages(pagetable, TRAMPOLINE, PGSIZE,
            (uint64_t)trampoline, PTE_R | PTE_X)
        < 0) {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trap_frame page just below the trampoline page, for
    // trampoline.S.
    if (map_pages(pagetable, TRAPFRAME, PGSIZE,
            (uint64_t)(p->trap_frame), PTE_R | PTE_W)
        < 0) {
        uvm_unmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_free_pagetable(pagetable_t pagetable, uint64_t sz)
{
    uvm_unmap(pagetable, TRAMPOLINE, 1, 0);
    uvm_unmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar_t initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void user_init(void)
{
    struct proc* p = alloc_proc();
    init_proc = p;

    // allocate one user page and copy initcode's instructions
    // and data into it.
    uvm_first(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    // prepare for the very first "return" from kernel to user.
    p->trap_frame->epc = 0; // user program counter
    p->trap_frame->sp = PGSIZE; // user stack pointer

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;

    release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
    uint64_t sz;
    struct proc* p = my_proc();

    sz = p->sz;
    if (n > 0) {
        if ((sz = uvm_alloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
            return -1;
        }
    } else if (n < 0) {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
    int i, pid;
    struct proc* np;
    struct proc* p = my_proc();

    // Allocate process.
    if ((np = alloc_proc()) == 0) {
        return -1;
    }

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        free_proc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // copy saved user registers.
    *(np->trap_frame) = *(p->trap_frame);

    // Cause fork to return 0 in the child.
    np->trap_frame->a0 = 0;

    // increment reference counts on open file descriptors.
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    release(&np->lock);

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc* p)
{
    struct proc* pp;

    for (pp = procs; pp < &procs[NPROC]; pp++) {
        if (pp->parent == p) {
            pp->parent = init_proc;
            wakeup(init_proc);
        }
    }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
    struct proc* p = my_proc();

    if (p == init_proc)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct file* f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup(p->parent);

    acquire(&p->lock);

    p->xstate = status;
    p->state = ZOMBIE;

    release(&wait_lock);

    // Jump into the scheduler, never to return.
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64_t addr)
{
    struct proc* pp;
    int havekids, pid;
    struct proc* p = my_proc();

    acquire(&wait_lock);

    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (pp = procs; pp < &procs[NPROC]; pp++) {
            if (pp->parent == p) {
                // make sure the child isn't still in exit() or swtch().
                acquire(&pp->lock);

                havekids = 1;
                if (pp->state == ZOMBIE) {
                    // Found one.
                    pid = pp->pid;
                    if (addr != 0 && copyout(p->pagetable, addr, (char*)&pp->xstate, sizeof(pp->xstate)) < 0) {
                        release(&pp->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    free_proc(pp);
                    release(&pp->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&pp->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || killed(p)) {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
    struct proc* p;
    struct cpu* c = my_cpu();

    c->proc = 0;
    for (;;) {
        // The most recent process to run may have had interrupts
        // turned off; enable them to avoid a deadlock if all
        // processes are waiting.
        intr_on();

        int found = 0;
        for (p = procs; p < &procs[NPROC]; p++) {
            acquire(&p->lock);
            if (p->state == RUNNABLE) {
                // Switch to chosen process.  It is the process's job
                // to release its lock and then reacquire it
                // before jumping back to us.
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->context, &p->context);

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
                found = 1;
            }
            release(&p->lock);
        }
        if (found == 0) {
            // nothing to run; stop running on this core until an interrupt.
            intr_on();
            __asm__ volatile("wfi");
        }
    }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// int_ena because int_ena is a property of this
// kernel thread, not this CPU. It should
// be proc->int_ena and proc->n_off, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
    int int_ena;
    struct proc* p = my_proc();

    if (!holding(&p->lock))
        panic("sched p->lock");
    if (my_cpu()->n_off != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    int_ena = my_cpu()->int_ena;
    swtch(&p->context, &my_cpu()->context);
    my_cpu()->int_ena = int_ena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
    struct proc* p = my_proc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to fork_ret.
void fork_ret(void)
{
    static int first = 1;

    // Still holding p->lock from scheduler.
    release(&my_proc()->lock);

    if (first) {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        fsinit(ROOTDEV);

        first = 0;
        // ensure other cores see first=0.
        __sync_synchronize();
    }

    usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void* chan, struct spinlock* lk)
{
    struct proc* p = my_proc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&p->lock); // DOC: sleeplock1
    release(lk);

    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    release(&p->lock);
    acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void* chan)
{
    struct proc* p;

    for (p = procs; p < &procs[NPROC]; p++) {
        if (p != my_proc()) {
            acquire(&p->lock);
            if (p->state == SLEEPING && p->chan == chan) {
                p->state = RUNNABLE;
            }
            release(&p->lock);
        }
    }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see user_trap() in trap.c).
int kill(int pid)
{
    struct proc* p;

    for (p = procs; p < &procs[NPROC]; p++) {
        acquire(&p->lock);
        if (p->pid == pid) {
            p->killed = 1;
            if (p->state == SLEEPING) {
                // Wake process from sleep().
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

void setkilled(struct proc* p)
{
    acquire(&p->lock);
    p->killed = 1;
    release(&p->lock);
}

int killed(struct proc* p)
{
    int k;

    acquire(&p->lock);
    k = p->killed;
    release(&p->lock);
    return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64_t dst, void* src, uint64_t len)
{
    struct proc* p = my_proc();
    if (user_dst) {
        return copyout(p->pagetable, dst, src, len);
    } else {
        memmove((char*)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void* dst, int user_src, uint64_t src, uint64_t len)
{
    struct proc* p = my_proc();
    if (user_src) {
        return copyin(p->pagetable, dst, src, len);
    } else {
        memmove(dst, (char*)src, len);
        return 0;
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
    static char* states[] = {
        [UNUSED] "unused",
        [USED] "used",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runble",
        [RUNNING] "run   ",
        [ZOMBIE] "zombie"
    };
    struct proc* p;
    char* state;

    printf("\n");
    for (p = procs; p < &procs[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d %s %s", p->pid, state, p->name);
        printf("\n");
    }
}
