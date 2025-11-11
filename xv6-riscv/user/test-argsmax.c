#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
#include "user/uspinlock.h"

#include <stdarg.h>

int success = 0;

void thread_func(uint64 a, uint64 b, uint64 c, uint64 d, uint64 e, uint64 f, uint64 g, uint64 h, 
                    uint64 i, uint64 j, uint64 k, uint64 l, uint64 m, uint64 n, uint64 o, uint64 p,
                    uint64 q, uint64 r, uint64 s, uint64 t, uint64 u, uint64 v, uint64 w, uint64 x,
                    uint64 y, uint64 z) {
    if(a == 25 && b == 24 && c == 23 && d == 22 && e == 21 && f == 20 && g == 19 &&
       h == 18 && i == 17 && j == 16 && k == 15 && l == 14 && m == 13 && n == 12 && o == 11 &&
       p == 10 && q == 9 && r == 8 && s == 7 && t == 6 && u == 5 && v == 4 && w == 3 &&
       x == 2 && y == 1 && z == 0) {
        success = 1;
    }

    printf("[.] inside thread_func with args: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", 
        (int)a, (int)b, (int)c, (int)d, (int)e, (int)f, (int)g, (int)h,
        (int)i, (int)j, (int)k, (int)l, (int)m, (int)n, (int)o, (int)p,
        (int)q, (int)r, (int)s, (int)t, (int)u, (int)v, (int)w, (int)x,
        (int)y, (int)z);

    thread_destroy();
}

void thread_join() {
    while(get_active_threads() != 1);
}

uint64 args[26] = {25, 24, 23, 22, 21, 20, 19,
                    18, 17, 16, 15, 14, 13, 12, 11,
                    10, 9, 8, 7, 6, 5, 4, 3, 2, 
                    1, 0};

int
main(int argc, char *argv[])
{
    printf("[*] Running test argsmax\n");

    thread_create((uint64) thread_func, (uint64) args, 0);
    
    thread_join();

    printf("[*] test argsmax %s\n", success ? "passed" : "failed");

    return 0;
}