// NOTICE 一定要注意这个头文件的顺序
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
int main()
{
    if (cpu_id() == 0) {
        console_init();
        printf_init();
        printf("\n");
        printf("xv6 kernel is booting\n");
        printf("\n");
        k_init(); // physical page allocator
        kvm_init(); // create kernel page table
        kvm_init_hart(); // turn on paging
        proc_init(); // process table
        trap_init(); // trap vectors
        trap_init_hart(); // install kernel trap vector
        plic_init(); // set up interrupt controller
        plic_init_hart(); // ask PLIC for device interrupts
        binit(); // buffer cache
        iinit(); // inode table
        file_init(); // file table
        virtio_disk_init(); // emulated hard disk
        user_init(); // first user process
        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0)
            ;
        __sync_synchronize();
        printf("hart %d starting\n", cpu_id());
        kvm_init_hart(); // turn on paging
        trap_init_hart(); // install kernel trap vector
        plic_init_hart(); // ask PLIC for device interrupts
    }

    scheduler();
}
