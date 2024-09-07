struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

// bio.c
void binit(void);
struct buf* bread(uint_t, uint_t);
void brelse(struct buf*);
void bwrite(struct buf*);
void bpin(struct buf*);
void bunpin(struct buf*);

// console.c
void console_init(void);
void console_intr(int);
void cons_putc(int);

// exec.c
int exec(char*, char**);

// file.c
struct file* file_alloc(void);
void fileclose(struct file*);
struct file* filedup(struct file*);
void file_init(void);
int fileread(struct file*, uint64_t, int n);
int filestat(struct file*, uint64_t addr);
int filewrite(struct file*, uint64_t, int n);

// fs.c
void fsinit(int);
int dirlink(struct inode*, char*, uint_t);
struct inode* dirlookup(struct inode*, char*, uint_t*);
struct inode* ialloc(uint_t, short);
struct inode* idup(struct inode*);
void iinit();
void ilock(struct inode*);
void iput(struct inode*);
void iunlock(struct inode*);
void iunlockput(struct inode*);
void iupdate(struct inode*);
int namecmp(const char*, const char*);
struct inode* namei(char*);
struct inode* nameiparent(char*, char*);
int readi(struct inode*, int, uint64_t, uint_t, uint_t);
void stati(struct inode*, struct stat*);
int writei(struct inode*, int, uint64_t, uint_t, uint_t);
void itrunc(struct inode*);

// ramdisk.c
void ramdiskinit(void);
void ramdiskintr(void);
void ramdiskrw(struct buf*);

// k_alloc.c
void* k_alloc(void);
void k_free(void*);
void k_init(void);

// log.c
void initlog(int, struct superblock*);
void log_write(struct buf*);
void begin_op(void);
void end_op(void);

// pipe.c
int pipealloc(struct file**, struct file**);
void pipeclose(struct pipe*, int);
int piperead(struct pipe*, uint64_t, int);
int pipewrite(struct pipe*, uint64_t, int);

// printf.c
int printf(char*, ...) __attribute__((format(printf, 1, 2)));
void panic(char*) __attribute__((noreturn));
void printf_init(void);

// proc.c
int cpu_id(void);
void exit(int);
int fork(void);
int growproc(int);
void proc_map_stacks(pagetable_t);
pagetable_t proc_pagetable(struct proc*);
void proc_free_pagetable(pagetable_t, uint64_t);
int kill(int);
int killed(struct proc*);
void setkilled(struct proc*);
struct cpu* my_cpu(void);
struct cpu* getmycpu(void);
struct proc* my_proc();
void proc_init(void);
void scheduler(void) __attribute__((noreturn));
void sched(void);
void sleep(void*, struct spinlock*);
void user_init(void);
int wait(uint64_t);
void wakeup(void*);
void yield(void);
int either_copyout(int user_dst, uint64_t dst, void* src, uint64_t len);
int either_copyin(void* dst, int user_src, uint64_t src, uint64_t len);
void proc_dump(void);

// swtch.S
void swtch(struct context*, struct context*);

// spinlock.c
void acquire(struct spinlock*);
int holding(struct spinlock*);
void init_lock(struct spinlock*, char*);
void release(struct spinlock*);
void push_off(void);
void pop_off(void);

// sleeplock.c
void acquiresleep(struct sleeplock*);
void releasesleep(struct sleeplock*);
int holdingsleep(struct sleeplock*);
void initsleeplock(struct sleeplock*, char*);

// string.c
int memcmp(const void*, const void*, uint_t);
void* memmove(void*, const void*, uint_t);
void* memset(void*, int, uint_t);
char* safestrcpy(char*, const char*, int);
int strlen(const char*);
int strncmp(const char*, const char*, uint_t);
char* strncpy(char*, const char*, int);

// syscall.c
void arg_int(int, int*);
int arg_str(int, char*, int);
void arg_addr(int, uint64_t*);
int fetch_str(uint64_t, char*, int);
int fetch_addr(uint64_t, uint64_t*);
void syscall();

// trap.c
extern uint_t ticks;
void trap_init(void);
void trap_init_hart(void);
extern struct spinlock ticks_lock;
void usertrapret(void);

// uart.c
void uart_init(void);
void uart_intr(void);
void uart_putc(int);
void uart_putc_sync(int);
int uart_getc(void);

// vm.c
void kvm_init(void);
void kvm_init_hart(void);
void k_vm_map(pagetable_t, uint64_t, uint64_t, uint64_t, int);
int map_pages(pagetable_t, uint64_t, uint64_t, uint64_t, int);
pagetable_t uvm_create(void);
void uvm_first(pagetable_t, uchar_t*, uint_t);
uint64_t uvm_alloc(pagetable_t, uint64_t, uint64_t, int);
uint64_t uvmdealloc(pagetable_t, uint64_t, uint64_t);
int uvmcopy(pagetable_t, pagetable_t, uint64_t);
void uvmfree(pagetable_t, uint64_t);
void uvm_unmap(pagetable_t, uint64_t, uint64_t, int);
void uvmclear(pagetable_t, uint64_t);
pte_t* walk(pagetable_t, uint64_t, int);
uint64_t walk_addr(pagetable_t, uint64_t);
int copyout(pagetable_t, uint64_t, char*, uint64_t);
int copyin(pagetable_t, char*, uint64_t, uint64_t);
int copyin_str(pagetable_t, char*, uint64_t, uint64_t);

// plic.c
void plic_init(void);
void plic_init_hart(void);
int plic_claim(void);
void plic_complete(int);

// virtio_disk.c
void virtio_disk_init(void);
void virtio_disk_rw(struct buf*, int);
void virtio_disk_intr(void);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
