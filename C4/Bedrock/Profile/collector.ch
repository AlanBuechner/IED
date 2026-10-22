// What the collector says about a session's program (IED docs/16): each time a collection stopped it, the live
// objects by type after each collection (GC::SetCensus), and allocations sampled with their call stacks
// (GC::SetAllocSampling), named by the reflected functions that hold their addresses.

include "session.ch"

namespace Profile
{
	// Turns the census and allocation sampling on for `session` (a program's own sampling rate is kept) and follows
	// each collection.
	void WatchCollector(Session* session);
	// Stops following collections and puts the program's census and sampling back as they were.
	void UnwatchCollector(Session* session);
	// The allocations sampled since the last call as Sample events, with the definitions of their call stacks. Called
	// by the flusher only.
	void QueueAllocSamples(Session* session);
}
