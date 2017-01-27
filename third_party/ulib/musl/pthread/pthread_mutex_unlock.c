#include "pthread_impl.h"

#include "futex_impl.h"

int pthread_mutex_unlock(pthread_mutex_t* m) {
    int waiters = m->_m_waiters;
    int cont;
    int type = m->_m_type & 15;

    if (type != PTHREAD_MUTEX_NORMAL) {
        if ((m->_m_lock & 0x7fffffff) != __thread_get_tid())
            return EPERM;
        if ((type & 3) == PTHREAD_MUTEX_RECURSIVE && m->_m_count)
            return m->_m_count--, 0;
    }
    cont = a_swap(&m->_m_lock, (type & 8) ? 0x40000000 : 0);
    if (waiters || cont < 0)
        __wake(&m->_m_lock, 1);
    return 0;
}
