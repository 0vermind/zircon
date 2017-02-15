#include "pthread_impl.h"
#include <stdint.h>
#include <string.h>

uintptr_t __stack_chk_guard;

void __init_ssp(void* entropy) {
    if (entropy)
        memcpy(&__stack_chk_guard, entropy, sizeof(uintptr_t));
    else
        __stack_chk_guard = (uintptr_t)&__stack_chk_guard * 1103515245;

    __pthread_self()->CANARY = __stack_chk_guard;
}

void __stack_chk_fail(void) {
    __builtin_trap();
}

__attribute__((__visibility__("hidden"))) void __stack_chk_fail_local(void);

weak_alias(__stack_chk_fail, __stack_chk_fail_local);
