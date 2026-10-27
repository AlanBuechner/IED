// What the host shares with its scripts: values read as volatile so no optimizer sees the faults coming, a barrier the
// faulting calls meet at, the cleanups a fault runs or leaves, and a lock a script faults holding.
include "Threading/mutex.ch"

namespace Faults
{
	volatile int32 g_zero = 0;
	volatile int64 g_null = 0;
	volatile int64 g_frame = 0;
	int32 g_arrived = 0;        // atomic: the calls at the barrier
	int32 g_expected = 0;       // how many calls the barrier waits for
	int32 g_cleanups = 0;       // atomic: the cleanups that ran
	int32 g_collections = 0;    // atomic: the collections another thread finished
	Threading::Mutex g_lock;
}
