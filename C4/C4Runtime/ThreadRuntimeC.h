#pragma once
// Threads that run C4 code (D-48). A thread is attached to the runtime before it runs C4 code: the collector scans
// it, and its thread_local globals and static members start (the compiler's `__c4_thread_start`). The main thread
// attaches after the program's globals start; a thread c4_thread_start started attaches before its entry runs;
// any other thread attaches itself with c4_thread_attach. Its thread_local objects are destroyed when it ends.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void c4_thread_attach(void);

void* c4_thread_start(void* entry, void* state);
void c4_thread_join(void* thread);
void c4_thread_detach(void* thread);
uint64_t c4_thread_current_id(void);
uint32_t c4_thread_hardware_count(void);
void c4_thread_yield(void);
// A spin-wait hint: lets the core's other hardware thread run and saves power while a loop waits.
void c4_cpu_relax(void);
void c4_thread_sleep_ns(int64_t ns);

bool c4_wait_on_address(void* address, void* expected, uint64_t size, int64_t timeoutNs);
void c4_wake_one(void* address);
void c4_wake_all(void* address);

// --- thread storage (ThreadStorageRuntimeC.c) ---
// `destroy(object)` runs when the calling thread ends, the last registered first; on the main thread, at exit
// with the globals' destructions (c4_at_exit).
void c4_thread_at_exit(void (*destroy)(void*), void* object);
// Runs and clears the calling thread's registrations (the collector calls it as the thread ends).
void c4_thread_run_exits(void);
void c4_thread_once_abandon(int32_t* guard);
void c4_thread_once_reentered(void);
// The calling thread ran the C runtime's initializers (the main thread).
bool c4_thread_is_main(void);

#ifdef __cplusplus
}
#endif
