#pragma once
// Concurrent marking (D-79): a mostly-concurrent mark-sweep (Boehm, Demers and Shenker). A thread of the collector's
// own runs each cycle. It forgets which heap pages were written, stops the program briefly to copy its roots, and marks
// from the copies while the program runs; then, a few times, it copies the roots again and rescans the marked objects
// on the pages the program wrote meanwhile. A final short stop marks from the roots and the pages written since, and
// finishes as a stop-the-world collection does (pools, finalizers, sweep). Objects allocated during the cycle start
// unmarked, so the ones that die during it go in it. The OS tracks the written pages, so compiled code needs no write
// barrier and native code may store pointers anywhere.

#include "GC/GCMark.h"

struct GCState;
struct GCThread;

enum
{
	GC_CYCLE_IDLE,
	GC_CYCLE_ASKED,        // an automatic collection asked for a cycle; the collector's thread starts it
	GC_CYCLE_MARKING,      // marking beside the program
	GC_CYCLE_FINISHING     // the second stop
};

typedef struct GCConcurrent
{
	GCMarker marker;           // the cycle's marker; its stack is the collector's scratch memory
	volatile long state;       // GC_CYCLE_*; waiters park on it
	volatile long hurry;       // a thread waits for the cycle: skip the concurrent work that is left
	int64_t threshold;         // live bytes from which automatic collections mark concurrently; -1: never
	int verify;                // check each cycle's marks against a fresh mark from the roots
	int trace;                 // print a line about each cycle to stderr (C4_GC_TRACE)
	int threadRunning;         // the collector's own thread has started
	const struct GCThread* thread; // it: no root, its stack holds only the collector's own addresses
	uint64_t pauseNs;          // the running cycle's stops so far
} GCConcurrent;

// Under the lock: whether an automatic collection now should be a concurrent cycle.
int gc_concurrent_wanted(const struct GCState* gc);
// Under the lock: asks the collector's thread for a cycle (none runs).
void gc_concurrent_ask(struct GCState* gc);
// Under the lock: a cycle is asked for or running.
int gc_concurrent_busy(const struct GCState* gc);

// On the collector's own (registered) thread, holding no lock: waits until a cycle is asked for and runs it.
void gc_concurrent_serve(struct GCState* gc, struct GCThread* self);

// Holding no lock: makes a cycle that is asked for or running finish now, and waits until it has.
void gc_concurrent_finish_now(struct GCState* gc);
