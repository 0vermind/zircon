#define _GNU_SOURCE
#include "libc.h"
#include "pthread_impl.h"
#include <sys/mman.h>

int pthread_getattr_np(pthread_t t, pthread_attr_t* a) {
    *a = (pthread_attr_t){};
    a->_a_detach = !!t->detached;
    a->_a_stackaddr = (uintptr_t)t->stack;
    a->_a_stacksize = t->stack_size;
    return 0;
}
