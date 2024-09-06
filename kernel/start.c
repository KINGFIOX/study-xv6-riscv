#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();

void timer_init();

// entry.S needs one stack per CPU. 这个会在 entry.S 中使用
__attribute__((aligned(16))) char stack0[4096 * NCPU];

// entry.S jumps here in machine mode on stack0.
void start()
{
    // set M Previous Privilege mode to Supervisor, for mret.
    unsigned long x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK; // m-mode prev priv
    x |= MSTATUS_MPP_S;
    w_mstatus(x);

    // set M Exception Program Counter to main, for mret.
    // requires gcc -mcmodel=medany
    w_mepc((uint64_t)main);

    // disable paging for now.
    w_satp(0);

    // delegate all interrupts and exceptions to supervisor mode.
    w_medeleg(0xffff);
    w_mideleg(0xffff);
    w_sie(r_sie() | SIE_SEIE /* s-mode external(外设) interrupt enable */ | SIE_STIE /* timer */ | SIE_SSIE /* 软件中断 */);

    // configure Physical Memory Protection to give supervisor mode
    // access to all of physical memory.
    // 0x3f_ffff_ffff_ffff ull -> (1 << 54) - 1
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf); // 访问权限

    // ask for clock interrupts.
    timer_init();

    // keep each CPU's hartid in its tp register, for cpu_id().
    int id = r_mhartid();
    w_tp(id);

    // switch to supervisor mode and jump to main().
    __asm__ volatile("mret");
}

// ask each hart to generate timer interrupts.
void timer_init()
{
    // enable supervisor-mode timer interrupts.
    w_mie(r_mie() | MIE_STIE);

    // enable the sstc extension (i.e. stimecmp).
    w_menvcfg(r_menvcfg() | (1L << 63));

    // allow supervisor to use stimecmp and time.
    w_mcounteren(r_mcounteren() | 2);

    // ask for the very first timer interrupt.
    w_stimecmp(r_time() + 1000000);
}
