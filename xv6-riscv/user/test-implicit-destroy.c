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

void thread_func(void) {
    printf("[.] started the thread function (tid = %d) \n", get_tid());
    for (int i = 0; i < 100000000; i++);
}

void thread_join() {
    while(get_active_threads() != 1);
}

int
main(int argc, char *argv[])
{
    printf("[*] Running implicit thread destroy test\n");

    uint64 args[6] = {0,0,0,0,0,0};    
    thread_create((uint64) thread_func, (uint64) args, -1);
    
    thread_join();

    printf("[*] Implicit thread destroy test complete\n");

    return 0;
}
