#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock ticks_lock;
uint_t ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int dev_intr();

void trap_init(void)
{
    init_lock(&ticks_lock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trap_init_hart(void)
{
    w_stvec((uint64_t)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void user_trap(void)
{
    int which_dev = 0;

    if ((r_sstatus() & SSTATUS_SPP) != 0) {
        panic("user_trap: not from user mode");
    }

    // send interrupts and exceptions to kerneltrap(),
    // since we're now in the kernel.
    w_stvec((uint64_t)kernelvec);

    struct proc* p = my_proc();

    // save user program counter.
    p->trap_frame->epc = r_sepc();

    if (r_scause() == 8) {
        // system call

        if (killed(p))
            exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        p->trap_frame->epc += 4;

        // an interrupt will change sepc, scause, and sstatus,
        // so enable only now that we're done with those registers.
        intr_on();

        syscall();
    } else if ((which_dev = dev_intr()) != 0) {
        // ok
    } else {
        printf("user_trap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
        printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
        setkilled(p);
    }

    if (killed(p))
        exit(-1);

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2)
        yield();

    usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
    struct proc* p = my_proc();

    // we're about to switch the destination of traps from
    // kerneltrap() to user_trap(), so turn off interrupts until
    // we're back in user space, where user_trap() is correct.
    intr_off();

    // send syscalls, interrupts, and exceptions to uservec in trampoline.S
    uint64_t trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
    w_stvec(trampoline_uservec);

    // set up trap_frame values that uservec will need when
    // the process next traps into the kernel.
    p->trap_frame->kernel_satp = r_satp(); // kernel page table
    p->trap_frame->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
    p->trap_frame->kernel_trap = (uint64_t)user_trap;
    p->trap_frame->kernel_hartid = r_tp(); // hartid for cpu_id()

    // set up the registers that trampoline.S's sret will use
    // to get to user space.

    // set S Previous Privilege mode to User.
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE; // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(p->trap_frame->epc);

    // tell trampoline.S the user page table to switch to.
    uint64_t satp = MAKE_SATP(p->pagetable);

    // jump to userret in trampoline.S at the top of memory, which
    // switches to the user page table, restores user registers,
    // and switches to user mode with sret.
    uint64_t trampoline_userret = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64_t))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap()
{
    int which_dev = 0;
    uint64_t sepc = r_sepc();
    uint64_t sstatus = r_sstatus();
    uint64_t scause = r_scause();

    if ((sstatus & SSTATUS_SPP) == 0) {
        panic("kerneltrap: not from supervisor mode");
    }
    if (intr_get() != 0) {
        panic("kerneltrap: interrupts enabled");
    }

    if ((which_dev = dev_intr()) == 0) {
        // interrupt or trap from an unknown source
        printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2 && my_proc() != 0)
        yield();

    // the yield() may have caused some traps to occur,
    // so restore trap registers for use by kernelvec.S's sepc instruction.
    w_sepc(sepc);
    w_sstatus(sstatus);
}

void clock_intr()
{
    if (cpu_id() == 0) {
        acquire(&ticks_lock);
        ticks++;
        wakeup(&ticks);
        release(&ticks_lock);
    }

    // ask for the next timer interrupt. this also clears
    // the interrupt request. 1000000 is about a tenth
    // of a second.
    w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int dev_intr()
{
    uint64_t scause = r_scause();

    if (scause == 0x8000000000000009L) {
        // this is a supervisor external interrupt, via PLIC.(platform level interrupt controller)

        // irq indicates which device interrupted.
        int irq = plic_claim();

        if (irq == UART0_IRQ) {
            uart_intr();
        } else if (irq == VIRTIO0_IRQ) {
            virtio_disk_intr();
        } else if (irq) {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // the PLIC allows each device to raise at most one
        // interrupt at a time; tell the PLIC the device is
        // now allowed to interrupt again.
        if (irq)
            plic_complete(irq);

        return 1;
    } else if (scause == 0x8000000000000005L) {
        // timer interrupt.
        clock_intr();
        return 2;
    } else {
        return 0;
    }
}
