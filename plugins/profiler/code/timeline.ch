// prof.timeline (docs/16 "The server does the heavy lifting"): what a timeline of `width` pixels shows of a range -
// each lane's zones, with runs of zones narrower than a pixel merged into one span that says how many it holds, so
// the answer stays the size of the screen however many zones the range covers. Lanes are threads, or with `tasks`
// the task names (docs/16 "Thread names and the by-task view"); then GPU queues, frames, collections, messages and
// plots in the range.

include "query.ch"

namespace Profiler
{
	enum class LaneMode { Threads, Tasks }

	struct TimelineAsk
	{
		public int64 fromNs = -1;
		public int64 toNs = -1;
		public int32 width = 1000;
		public LaneMode mode;
		public int32 mostDepth = 64;
		public int32 mostMessages = 300;
	}

	// The answer's JSON (docs/16 "prof.timeline"); the caller holds the capture's lock.
	Core::String Timeline(const Capture& capture, const TimelineAsk& ask);
}
