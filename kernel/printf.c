//
// formatted console output -- printf, panic.
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

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
    struct spinlock lock;
    int locking;
} pr; // 这种语法, 其实是声明匿名结构体, 并创建了一个对象 pr

static char digits[] = "0123456789abcdef"; // ascii, 查表

static void print_int(long long xx, int base, int sign)
{

    unsigned long long x;
    if (sign && (xx < 0)) {
        x = -xx;
        sign = 1; // 确实是负数, 显示 '-'
    } else {
        x = xx;
    }

    char buf[16];
    int i = 0;
    do {
        buf[i++] = digits[x % base]; // 低位, 低字节
    } while ((x /= base) != 0);

    if (sign) {
        buf[i++] = '-';
    }

    while (--i >= 0) {
        // 倒过来打印, 符合人的书写方式
        cons_putc(buf[i]);
    }
}

/**
 * @brief 打印地址
 *
 * @param x
 */
static void print_ptr(uint64 x)
{
    cons_putc('0');
    cons_putc('x');
    for (int i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4) {
        cons_putc(digits[x >> (sizeof(uint64) * 8 - 4)]);
    }
}

// Print to the console.
int printf(char* fmt, ...)
{

    int locking = pr.locking;
    if (locking) {
        acquire(&pr.lock);
    }

    va_list ap;
    va_start(ap, fmt);

    // 一个自动机
    for (int i = 0, cx; (cx = fmt[i] & 0xff) != 0 /* 没有到 null 字符 */; i++) {
        if (cx != '%') {
            cons_putc(cx);
            continue;
        }
        // cx == %
        i++;
        int c0 = fmt[i + 0] & 0xff; // 获取一个字符
        int c1 = 0, c2 = 0;
        if (c0) {
            c1 = fmt[i + 1] & 0xff;
        }
        if (c1) {
            c2 = fmt[i + 2] & 0xff;
        }
        if (c0 == 'd') {
            print_int(va_arg(ap, int), 10, 1);
        } else if (c0 == 'l' && c1 == 'd') {
            print_int(va_arg(ap, uint64), 10, 1);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
            print_int(va_arg(ap, uint64), 10, 1);
            i += 2;
        } else if (c0 == 'u') {
            print_int(va_arg(ap, int), 10, 0);
        } else if (c0 == 'l' && c1 == 'u') {
            print_int(va_arg(ap, uint64), 10, 0);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
            print_int(va_arg(ap, uint64), 10, 0);
            i += 2;
        } else if (c0 == 'x') {
            print_int(va_arg(ap, int), 16, 0);
        } else if (c0 == 'l' && c1 == 'x') {
            print_int(va_arg(ap, uint64), 16, 0);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
            print_int(va_arg(ap, uint64), 16, 0);
            i += 2;
        } else if (c0 == 'p') {
            print_ptr(va_arg(ap, uint64));
        } else if (c0 == 's') {
            char* s = 0;
            if ((s = va_arg(ap, char*)) == 0) {
                s = "(null)";
            }
            for (; *s; s++) {
                cons_putc(*s);
            }
        } else if (c0 == '%') {
            cons_putc('%');
        } else if (c0 == 0) {
            break;
        } else {
            // Print unknown % sequence to draw attention.
            cons_putc('%');
            cons_putc(c0);
        }
    }
    va_end(ap);

    if (locking) {
        release(&pr.lock);
    }

    return 0;
}

void panic(char* s)
{
    pr.locking = 0;
    printf("panic: ");
    printf("%s\n", s);
    panicked = 1; // freeze uart output from other CPUs
    for (;;)
        ;
}

void printf_init(void)
{
    init_lock(&pr.lock, "pr");
    pr.locking = 1;
}
