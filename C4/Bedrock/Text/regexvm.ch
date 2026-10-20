// The Pike VM a Regex runs on: every thread of the program advances over the text together, one character at a step,
// so a match costs the text's length times the program's size at most. Threads sit in priority order (alternatives
// and greedy repetition first); a thread that matches drops the threads after it, which gives the leftmost-first
// match a backtracking engine would find.

include "regex.ch"

namespace Core
{
	// What a byte that is not UTF-8 reads as: past every code point, so only '.' takes it.
	const int32 kRegexInvalidByte = 0x110000;

	// The threads of one step: their program counters and group bounds (`slots` per thread, back to back), and the
	// step each instruction was last added in.
	struct RegexThreads
	{
		public Array<int32> pcs;
		public Array<int64> bounds;
		public Array<int64> seen;
		public int64 stamp;

		// Empties the list for the next step.
		public void Begin(int64 programSize);
		public void Swap(RegexThreads& other);
	}

	// A frame of AddThread's walk: an instruction to visit, or (slot >= 0) a group bound to put back.
	struct RegexFrame
	{
		public int32 pc;
		public int32 slot;
		public int64 value;
	}

	// The machine: the program, and the walk stack and group bounds it reuses from step to step.
	class RegexVm
	{
		[operator("create")] public void Init(const Array<RegexInst>& program, const Array<RegexRange>& ranges, StringView text, int64 slots);

		// Follows the jumps, splits, saves and assertions from `pc` at byte `at`, adding each instruction that takes a
		// character (or matches) to `list` with the bounds in `scratch` at that point, in priority order.
		public void AddThread(RegexThreads& list, int32 pc, int64 at);
		// A thread of the whole program starting at byte `at`, its groups unset.
		public void Start(RegexThreads& list, int64 at);
		// Thread `i` of `from`'s bounds into `scratch`.
		public void Load(const RegexThreads& from, int64 i);
		// Whether the thread's instruction takes code point `c` (-1: none, at the end).
		public bool Takes(const RegexInst& inst, int32 c) const;

		public int64 slots;
		public Array<int64> scratch;

		private void Visit(int32 pc);

		private const Array<RegexInst>* program;
		private const Array<RegexRange>* ranges;
		private StringView text;
		private Array<RegexFrame> walk;
	}
}
