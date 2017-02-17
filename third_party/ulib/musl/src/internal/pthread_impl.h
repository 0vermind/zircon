#pragma once

#include "atomic.h"
#include "ksigaction.h"
#include "libc.h"
#include "pthread_arch.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <threads.h>

#include <magenta/tls.h>
#include <runtime/thread.h>
#include <runtime/tls.h>

#define pthread __pthread

// This is what the thread pointer points to directly.  On TLS_ABOVE_TP
// machines, the size of this is part of the ABI known to the compiler
// and linker.
typedef struct {
    // The position of this pointer is part of the ABI on x86.
    // It has the same value as the thread pointer itself.
    uintptr_t tp;
    void** dtv;
} tcbhead_t;

// The locations of these fields is part of the ABI known to the compiler.
typedef struct {
    uintptr_t stack_guard;
    uintptr_t unsafe_sp;
} tp_abi_t;

struct pthread {
#ifndef TLS_ABOVE_TP
    // These must be the very first members.
    tcbhead_t head;
    tp_abi_t abi;
#endif

    mxr_thread_t mxr_thread;

    void* tsd[PTHREAD_KEYS_MAX];
    int tsd_used;
    int errno_value;

    volatile atomic_int cancel, canceldisable, cancelasync;
    int detached;
    unsigned char* map_base;
    size_t map_size;
    void* stack;
    size_t stack_size;
    void* start_arg;
    void* (*start)(void*);
    void* result;
    struct __ptcb* cancelbuf;
    pthread_attr_t attr;
    volatile int dead;
    int unblock_cancel;
    volatile int timer_id;
    locale_t locale;
    mtx_t killlock;
    mtx_t exitlock;
    mtx_t startlock;
    unsigned long sigmask[_NSIG / 8 / sizeof(long)];
    char* dlerror_buf;
    int dlerror_flag;
    void* stdio_locks;

#ifdef TLS_ABOVE_TP
    // These must be the very last members.
    tp_abi_t abi;
    tcbhead_t head;
#endif
};

struct __timer {
    int timerid;
    pthread_t thread;
};

#ifdef TLS_ABOVE_TP
#define PTHREAD_TP_OFFSET offsetof(struct pthread, head)
#else
#define PTHREAD_TP_OFFSET 0
#endif

#define TP_OFFSETOF(field) \
    ((ptrdiff_t)offsetof(struct pthread, field) - PTHREAD_TP_OFFSET)

static_assert(TP_OFFSETOF(head) == 0,
              "ABI tcbhead_t misplaced in struct pthread");

#ifdef ABI_TCBHEAD_SIZE
static_assert((sizeof(struct pthread) -
               offsetof(struct pthread, head)) == ABI_TCBHEAD_SIZE,
              "ABI tcbhead_t misplaced in struct pthread");
#endif

#if defined(__x86_64__) || defined(__aarch64__)
// The tlsdesc.s assembly code assumes this, though it's not part of the ABI.
static_assert(TP_OFFSETOF(head.dtv) == 8, "dtv misplaced in struct pthread");
#endif

static_assert(TP_OFFSETOF(abi.stack_guard) == MX_TLS_STACK_GUARD_OFFSET,
              "stack_guard not at ABI-mandated offset from thread pointer");
static_assert(TP_OFFSETOF(abi.unsafe_sp) == MX_TLS_UNSAFE_SP_OFFSET,
              "unsafe_sp not at ABI-mandated offset from thread pointer");

static inline void* pthread_to_tp(struct pthread* thread) {
    return (void*)((char*)thread + PTHREAD_TP_OFFSET);
}

static inline struct pthread* tp_to_pthread(void* tp) {
    return (struct pthread*)((char*)tp - PTHREAD_TP_OFFSET);
}

#ifndef DTP_OFFSET
#define DTP_OFFSET 0
#endif

#define SIGTIMER 32
#define SIGCANCEL 33

#define SIGALL_SET ((sigset_t*)(const unsigned long long[2]){-1, -1})
#define SIGPT_SET                                                                     \
    ((sigset_t*)(const unsigned long[_NSIG / 8 / sizeof(long)]){[sizeof(long) == 4] = \
                                                                    3UL               \
                                                                    << (32 * (sizeof(long) > 4))})
#define SIGTIMER_SET ((sigset_t*)(const unsigned long[_NSIG / 8 / sizeof(long)]){0x80000000})

extern void* __pthread_tsd_main[];
extern volatile size_t __pthread_tsd_size;

static inline pthread_t __pthread_self(void) {
    return tp_to_pthread(mxr_tp_get());
}

static inline pid_t __thread_get_tid(void) {
    // TODO(kulakowski) Replace this with the current thread handle's
    // ID when magenta exposes those.
    return (pid_t)(intptr_t)__pthread_self();
}

// Signal n (or all, for -1) threads on a pthread_cond_t or cnd_t.
void __private_cond_signal(void* condvar, int n);

int __libc_sigaction(int, const struct sigaction*, struct sigaction*);
int __libc_sigprocmask(int, const sigset_t*, sigset_t*);

void __vm_wait(void);
void __vm_lock(void);
void __vm_unlock(void);

// These are guaranteed to only return 0, EINVAL, or ETIMEDOUT.
int __timedwait(atomic_int*, int, clockid_t, const struct timespec*);
int __timedwait_cp(atomic_int*, int, clockid_t, const struct timespec*);

void __acquire_ptc(void);
void __release_ptc(void);
void __inhibit_ptc(void);

void __block_all_sigs(void*);
void __block_app_sigs(void*);
void __restore_sigs(void*);

void __pthread_tsd_run_dtors(void);

static inline int __sigaltstack(const stack_t* restrict ss, stack_t* restrict old) {
    return 0;
}

static inline int __rt_sigprocmask(int how, const sigset_t* restrict set,
                                   sigset_t* restrict old_set, size_t sigmask_size) {
    return 0;
}

static inline int __rt_sigaction(int sig, const struct k_sigaction* restrict action,
                                 struct k_sigaction* restrict old_action,
                                 size_t sigaction_mask_size) {
    return 0;
}

static inline int __rt_sigpending(sigset_t* set, size_t sigset_size) {
    return 0;
}

static inline int __rt_sigsuspend(const sigset_t* set, size_t sigset_size) {
    return 0;
}

static inline int __rt_sigtimedwait(const sigset_t* restrict set, siginfo_t* restrict info,
                                    const struct timespec* restrict timeout, size_t sigset_size) {
    return 0;
}

static inline int __rt_sigqueueinfo(pid_t pid, int sig, siginfo_t* info) {
    return 0;
}

#define DEFAULT_PTHREAD_ATTR                                                  \
    ((pthread_attr_t){                                                        \
        ._a_stacksize = 81920,                                                \
        ._a_guardsize = PAGE_SIZE,                                            \
    })
