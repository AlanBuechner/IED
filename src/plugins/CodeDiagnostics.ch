// A project plugin's compile diagnostics in its workspace's Error List (docs/26 section 2): each
// "<file> (<line>:<column>) <severity>: <message>" line the compiler wrote, on the plugin's code file of that name, from
// the source "plugin <id>"; a build that compiles clears them. A built-in or app plugin's code is not in a workspace:
// its diagnostics stay in its summary and the server log.

include "Code.ch"

namespace Ied
{
	// With the server lock held; `diagnostics` empty: the build compiled.
	void PublishCodeDiagnostics(Server* server, CodeInstance* instance, Core::StringView diagnostics);

	// One line of a compiler diagnostic, read.
	class CodeProblem
	{
		public Core::String m_File;      // as the compiler names it
		public int64 m_Line;             // 1-based
		public int64 m_Column;
		public int64 m_Severity;         // 1 error, 2 warning, 3 information
		public Core::String m_Message;
	}

	// The problems of a compiler's diagnostics text, in order; the source lines and carets between them are skipped.
	Core::Array<CodeProblem> ReadCodeProblems(Core::StringView diagnostics);
}
