// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void init_lock(struct spinlock* lk, char* name)
{
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void acquire(struct spinlock* lk)
{
    // 临界区域内: 关中断
    push_off(); // disable interrupts to avoid deadlock.
    if (holding(lk)) {
        panic("acquire");
    }

    // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
    //   a5 = 1
    //   s1 = &lk->locked
    //   amoswap.w.aq a5, a5, (s1)
    while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
        ;

    // Tell the C compiler and the processor to not move loads or stores
    // past this point, to ensure that the critical section's memory
    // references happen strictly after the lock is acquired.
    // On RISC-V, this emits a fence instruction.
    // 给编译器看: 指令调度不会跨越
    __sync_synchronize();

    // Record info about lock acquisition for holding() and debugging.
    lk->cpu = my_cpu();
}

// Release the lock.
void release(struct spinlock* lk)
{
    if (!holding(lk)) {
        panic("release"); // 未持有锁, 但是却释放了
    }

    lk->cpu = 0;

    // Tell the C compiler and the CPU to not move loads or stores
    // past this point, to ensure that all the stores in the critical
    // section are visible to other CPUs before the lock is released,
    // and that loads in the critical section occur strictly before
    // the lock is released.
    // On RISC-V, this emits a fence instruction.
    __sync_synchronize();

    // Release the lock, equivalent to lk->locked = 0.
    // This code doesn't use a C assignment, since the C standard
    // implies that an assignment might be implemented with
    // multiple store instructions.
    // On RISC-V, sync_lock_release turns into an atomic swap:
    //   s1 = &lk->locked
    //   amoswap.w zero, zero, (s1)
    __sync_lock_release(&lk->locked);

    pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int holding(struct spinlock* lk)
{
    return (lk->locked && lk->cpu == my_cpu());
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.

// push_off/pop_off 与 intr_off/intr_on 类似
// 只不过 push/pop_off 需要个数上匹配

/**
 * @brief 这里单独处理 n_off = 0 的情况。int_ena 保存的是: push_off 之前的 intr 的状态
 *
 */

void push_off(void)
{
    int old = intr_get(); // 获取 关中断之前的 标志位
    intr_off(); // 关中断
    if (my_cpu()->n_off == 0) {
        my_cpu()->int_ena = old;
    }
    my_cpu()->n_off += 1; // 增加嵌套计数器
}

void pop_off(void)
{
    struct cpu* c = my_cpu();
    if (intr_get()) {
        panic("pop_off - interruptible");
    }
    if (c->n_off < 1) {
        panic("pop_off");
    }
    c->n_off -= 1;
    //
    if (c->n_off == 0 && c->int_ena) {
        intr_on();
    }
}
