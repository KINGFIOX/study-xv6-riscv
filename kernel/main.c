// NOTICE 一定要注意这个头文件的顺序
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void main()
{
    if (cpu_id() == 0) {
        console_init();
        printf_init();
        printf("\n");
        printf("xv6 kernel is booting\n");
        printf("\n");
        k_init(); // physical page allocator
        k_vm_init(); // create kernel page table
        k_vm_init_hart(); // turn on paging
        procinit(); // process table
        trapinit(); // trap vectors
        trapinithart(); // install kernel trap vector
        plicinit(); // set up interrupt controller
        plicinithart(); // ask PLIC for device interrupts
        binit(); // buffer cache
        iinit(); // inode table
        file_init(); // file table
        virtio_disk_init(); // emulated hard disk
        userinit(); // first user process
        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0)
            ;
        __sync_synchronize();
        printf("hart %d starting\n", cpu_id());
        k_vm_init_hart(); // turn on paging
        trapinithart(); // install kernel trap vector
        plicinithart(); // ask PLIC for device interrupts
    }

    scheduler();
}
