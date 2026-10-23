include "bedrock.ch"

namespace Spans
{
	struct Span
	{
		public int64 start;   // inclusive
		public int64 end;     // exclusive
	}

	class SpanSet
	{
		// Adds every integer of [start, end); a range with end <= start does nothing.
		public void Add(int64 start, int64 end);
		// Removes every integer of [start, end); a range with end <= start does nothing.
		public void Remove(int64 start, int64 end);
		public bool Contains(int64 point) const;
		// How many disjoint spans the set is: spans that touch or overlap are one ([1,3) and [3,5) are [1,5)).
		public int64 Count() const;
		// How many integers the set holds.
		public int64 Covered() const;
		// The spans, in ascending order.
		public Core::Array<Span> All() const;

		private Core::Array<Span> spans;
		private int64 covered;

		// The index of the first span ending at or after `point` (one that touches or holds it).
		private int64 FirstEndingFrom(int64 point) const;
		// The index of the first span starting after `point`.
		private int64 FirstStartingAfter(int64 point) const;
		private void Replace(int64 first, int64 end, const Span* with, int64 count);
	}
}
