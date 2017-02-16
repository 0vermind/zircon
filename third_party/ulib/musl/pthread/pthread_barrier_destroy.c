#include "futex_impl.h"
#include "pthread_impl.h"

int pthread_barrier_destroy(pthread_barrier_t* b) {
    if (b->_b_limit < 0) {
        if (b->_b_lock) {
            int v;
            a_or(&b->_b_lock, INT_MIN);
            while ((v = b->_b_lock) & INT_MAX)
                __wait(&b->_b_lock, 0, v);
        }
        __vm_wait();
    }
    return 0;
}
