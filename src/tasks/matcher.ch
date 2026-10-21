// Problem matchers (docs/06 "Errors: problem matchers"): a plugin's `problemMatchers` turn lines of a task's output into
// problems. Patterns match consecutive lines in order and a problem is complete when the last one matches, the earlier
// lines' groups carrying over (multi-line matchers); a last pattern with `loop` goes on matching problems from the
// lines after it, until one does not.

include "bedrock.ch"

namespace Ied
{
	class MatcherPattern
	{
		public Core::Regex regex;
		public int32 file;   // group numbers; 0 for none
		public int32 line;
		public int32 column;
		public int32 endLine;
		public int32 endColumn;
		public int32 severity;
		public int32 code;
		public int32 message;
		public bool loop;
	}

	class ProblemMatcher
	{
		public Core::String id;
		public Core::String source;            // the Error List's source column; "build" by default
		public Core::Array<MatcherPattern*> patterns;
		public Core::HashMap<Core::String, Core::String> severityMap;   // the line's word (lower case) to error/warning/info/hint
		public Core::String defaultSeverity;
		public Core::String fileKind;          // absolute, relative, search
		public Core::Array<Core::String> roots;
	}

	// What a matcher read; numbers are 1-based and 0 when the line did not say.
	class MatchedProblem
	{
		public Core::String file;
		public int64 line;
		public int64 column;
		public int64 endLine;
		public int64 endColumn;
		public Core::String severity;          // error, warning, info or hint
		public Core::String code;
		public Core::String message;
	}

	// A `problemMatchers` entry; null, with `problem`, when a pattern does not compile.
	ProblemMatcher* ReadProblemMatcher(const Serialization::DomNode& node, Core::String& problem);

	// One matcher reading one stream, line by line.
	class MatcherState
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(ProblemMatcher* matcher);

		// Whether `line` completes a problem, then in `found`.
		public bool Feed(Core::StringView line, MatchedProblem& found);

		// The line matched pattern `index`: the sequence goes on, or a problem is complete.
		private bool Advance(int64 index, const Core::RegexMatch& match, MatchedProblem& found);
		private void Reset();

		private ProblemMatcher* matcher;
		private int64 next;
		private MatchedProblem partial;
	}
}
