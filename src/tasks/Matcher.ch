// Problem matchers (docs/06 "Errors: problem matchers"): a plugin's `problemMatchers` turn lines of a task's output into
// problems. Patterns match consecutive lines in order and a problem is complete when the last one matches, the earlier
// lines' groups carrying over (multi-line matchers); a last pattern with `loop` goes on matching problems from the
// lines after it, until one does not.

include "bedrock.ch"

namespace Ied
{
	class MatcherPattern
	{
		public Core::Regex m_Regex;
		public int32 m_File;   // group numbers; 0 for none
		public int32 m_Line;
		public int32 m_Column;
		public int32 m_EndLine;
		public int32 m_EndColumn;
		public int32 m_Severity;
		public int32 m_Code;
		public int32 m_Message;
		public bool m_Loop;
	}

	class ProblemMatcher
	{
		public Core::String m_Id;
		public Core::String m_Source;            // the Error List's source column; "build" by default
		public Core::Array<MatcherPattern*> m_Patterns;
		public Core::HashMap<Core::String, Core::String> m_SeverityMap;   // the line's word (lower case) to error/warning/info/hint
		public Core::String m_DefaultSeverity;
		public Core::String m_FileKind;          // absolute, relative, search
		public Core::Array<Core::String> m_Roots;
	}

	// What a matcher read; numbers are 1-based and 0 when the line did not say.
	class MatchedProblem
	{
		public Core::String m_File;
		public int64 m_Line;
		public int64 m_Column;
		public int64 m_EndLine;
		public int64 m_EndColumn;
		public Core::String m_Severity;          // error, warning, info or hint
		public Core::String m_Code;
		public Core::String m_Message;
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

		private ProblemMatcher* m_Matcher;
		private int64 m_Next;
		private MatchedProblem m_Partial;
	}
}
