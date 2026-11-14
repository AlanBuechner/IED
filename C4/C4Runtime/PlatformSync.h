#pragma once
// The locks the runtime keeps its own state under, on Windows' slim reader-writer locks and condition variables or on
// pthreads: a reader-writer lock, and a lock with a condition to wait on. Each starts with its *_INIT value, so a
// static one needs no start-up code.

#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>

typedef SRWLOCK c4_rwlock;
#define C4_RWLOCK_INIT SRWLOCK_INIT
static inline void c4_rwlock_read(c4_rwlock* lock) { AcquireSRWLockShared(lock); }
static inline void c4_rwlock_done_reading(c4_rwlock* lock) { ReleaseSRWLockShared(lock); }
static inline void c4_rwlock_write(c4_rwlock* lock) { AcquireSRWLockExclusive(lock); }
static inline void c4_rwlock_done_writing(c4_rwlock* lock) { ReleaseSRWLockExclusive(lock); }

typedef SRWLOCK c4_lock;
#define C4_LOCK_INIT SRWLOCK_INIT
static inline void c4_lock_take(c4_lock* lock) { AcquireSRWLockExclusive(lock); }
static inline void c4_lock_give(c4_lock* lock) { ReleaseSRWLockExclusive(lock); }

typedef CONDITION_VARIABLE c4_condition;
#define C4_CONDITION_INIT CONDITION_VARIABLE_INIT
// Gives `lock` up while it waits and holds it again when it returns (which may be early).
static inline void c4_condition_wait(c4_condition* condition, c4_lock* lock) { SleepConditionVariableSRW(condition, lock, INFINITE, 0); }
static inline void c4_condition_wake_all(c4_condition* condition) { WakeAllConditionVariable(condition); }

static inline uint32_t c4_os_thread_id(void) { return (uint32_t)GetCurrentThreadId(); }
#else
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef pthread_rwlock_t c4_rwlock;
#define C4_RWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
static inline void c4_rwlock_read(c4_rwlock* lock) { pthread_rwlock_rdlock(lock); }
static inline void c4_rwlock_done_reading(c4_rwlock* lock) { pthread_rwlock_unlock(lock); }
static inline void c4_rwlock_write(c4_rwlock* lock) { pthread_rwlock_wrlock(lock); }
static inline void c4_rwlock_done_writing(c4_rwlock* lock) { pthread_rwlock_unlock(lock); }

typedef pthread_mutex_t c4_lock;
#define C4_LOCK_INIT PTHREAD_MUTEX_INITIALIZER
static inline void c4_lock_take(c4_lock* lock) { pthread_mutex_lock(lock); }
static inline void c4_lock_give(c4_lock* lock) { pthread_mutex_unlock(lock); }

typedef pthread_cond_t c4_condition;
#define C4_CONDITION_INIT PTHREAD_COND_INITIALIZER
static inline void c4_condition_wait(c4_condition* condition, c4_lock* lock) { pthread_cond_wait(condition, lock); }
static inline void c4_condition_wake_all(c4_condition* condition) { pthread_cond_broadcast(condition); }

static inline uint32_t c4_os_thread_id(void) { return (uint32_t)syscall(SYS_gettid); }
#endif
