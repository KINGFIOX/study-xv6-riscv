// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void free_range(void* pa_start, void* pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
    struct run* next;
};

struct {
    struct spinlock lock;
    struct run* freelist; // 空闲链表
} kmem;

void k_init()
{
    init_lock(&kmem.lock, "kmem");
    free_range(end, (void*)PHYSTOP);
}

void free_range(void* pa_start, void* pa_end)
{
    for (char* p = (char*)PGROUNDUP((uint64)pa_start); p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
        k_free(p); // p 是 一个 page
    }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to k_alloc().  (The exception is when
// initializing the allocator; see k_init above.)
void k_free(void* pa)
{

    if (((uint64)pa % PGSIZE) != 0 /* 对齐 ? */
        || (char*)pa < end
        || (uint64)pa >= PHYSTOP) {
        panic("k_free");
    }

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    struct run* r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void* k_alloc(void)
{
    acquire(&kmem.lock);
    struct run* r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
    }
    release(&kmem.lock);
    if (r) {
        memset((char*)r, 5, PGSIZE); // fill with junk
    }
    return (void*)r;
}
