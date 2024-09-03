//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100
#define C(x) ((x) - '@') // Control-x

//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
//
void cons_putc(int c)
{
    if (c == BACKSPACE) {
        // if the user typed backspace, overwrite with a space.
        uart_putc_sync('\b');
        uart_putc_sync(' ');
        uart_putc_sync('\b');
    } else {
        uart_putc_sync(c);
    }
}

struct {
    struct spinlock lock;

    // input
#define INPUT_BUF_SIZE 128
    char buf[INPUT_BUF_SIZE];
    uint r; // Read index
    uint w; // Write index
    uint e; // Edit index
} cons;

//
// user write()s to the console go here.
//
int console_write(int user_src, uint64 src, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        char c;
        if (either_copyin(&c, user_src, src + i, 1) == -1)
            break;
        uart_putc(c);
    }

    return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int console_read(int user_dst, uint64 dst, int n)
{
    uint target;
    int c;
    char cbuf;

    target = n;
    acquire(&cons.lock);
    while (n > 0) {
        // wait until interrupt handler has put some
        // input into cons.buffer.
        while (cons.r == cons.w) {
            if (killed(myproc())) {
                release(&cons.lock);
                return -1;
            }
            sleep(&cons.r, &cons.lock);
        }

        c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

        if (c == C('D')) { // end-of-file
            if (n < target) {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                cons.r--;
            }
            break;
        }

        // copy the input byte to the user-space buffer.
        cbuf = c;
        if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
            break;

        dst++;
        --n;

        if (c == '\n') {
            // a whole line has arrived, return to
            // the user-level read().
            break;
        }
    }
    release(&cons.lock);

    return target - n;
}

//
// the console input interrupt handler.
// uart_intr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up console_read() if a whole line has arrived.
//
void console_intr(int c)
{
    acquire(&cons.lock);

    switch (c) {
    case C('P'): // Print process list.
        procdump();
        break;
    case C('U'): // Kill line.
        while (cons.e != cons.w && cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
            cons.e--;
            cons_putc(BACKSPACE);
        }
        break;
    case C('H'): // Backspace
    case '\x7f': // Delete key
        if (cons.e != cons.w) {
            cons.e--;
            cons_putc(BACKSPACE);
        }
        break;
    default:
        if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
            c = (c == '\r') ? '\n' : c;

            // echo back to the user.
            cons_putc(c);

            // store for consumption by console_read().
            cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

            if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) {
                // wake up console_read() if a whole line (or end-of-file)
                // has arrived.
                cons.w = cons.e;
                wakeup(&cons.r);
            }
        }
        break;
    }

    release(&cons.lock);
}

void console_init(void)
{
    init_lock(&cons.lock, "cons");

    uart_init();

    // connect read and write system calls
    // to console_read and console_write.
    devsw[CONSOLE].read = console_read;
    devsw[CONSOLE].write = console_write;
}
