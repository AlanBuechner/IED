// A Regex pattern's tree (regexparse.ch) turned into the program the VM runs (regexvm.ch): one instruction per
// character, class or assertion, Split and Jump for alternatives and repetition, Save for group bounds. A counted
// repetition is its body written out that many times.

include "regexparse.ch"

namespace Core
{
	// The most instructions a program may have: x{1000}{...} nested past this is refused.
	const int32 kRegexMaxProgram = 200000;

	class RegexCompiler
	{
		[operator("create")] public void Init(const RegexParser& tree) { this.tree = &tree; }

		public int32 Emit(RegexOp op, int32 x, int32 y);
		public int32 Here() const { return (int32)program.Count(); }
		public void Node(int32 index);

		public Array<RegexInst> program;
		public Array<RegexRange> ranges;
		public bool tooLarge;

		private void Literal(const RegexNode& node);
		private void Class(const RegexNode& node);
		private void EmitClass(const Array<RegexRange>& set);
		private void Alternation(const RegexNode& node);
		private void Repeat(const RegexNode& node);
		private void Prefer(int32 split, bool greedy);
		private void Group(const RegexNode& node);

		private const RegexParser* tree;
	}
}
