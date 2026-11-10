// The timeline's lane order (docs/16 "Lane order"): the GPU queues first, by kind - Direct (or Graphics), Compute,
// Copy (or Transfer), then any other - and then the threads in the order the program gave (ThreadOrder: names and
// patterns), else in the order they appeared. The only order IED decides itself is the GPU queues'.

include "Query.ch"

namespace Profiler
{
	// Whether `pattern` matches all of `name`, case ignored: `*` any run of characters, `?` one.
	bool NameMatches(Core::StringView pattern, Core::StringView name);

	// Where a thread goes in the program's order: at the first entry other than "*" that one of its names matches (its
	// names tried in the order it had them, so a worker renamed for a task stays a worker), else where "*" is, else
	// after every entry. 0 for every thread when the program gave no order.
	int64 ThreadRank(const Capture& capture, const ThreadTrack* track);

	// `tracks` (indexes into the capture's tracks) in lane order: by rank, threads of one rank in the order they
	// appeared.
	void OrderThreads(const Capture& capture, Core::Array<int64>& tracks);

	// A GPU queue's place by its kind, from its name: 0 Direct or Graphics, 1 Compute, 2 Copy or Transfer, 3 any other.
	int32 GpuQueueRank(Core::StringView name);

	// The capture's GPU queues (indexes into its contexts) in lane order: by kind, queues of one kind as defined.
	Core::Array<int64> OrderedGpus(const Capture& capture);
}
