// After the AI writes a file (docs/24 "After an AI edit"): the language server that serves it formats it first, when the
// language's plugin asks (an LSP profile's formatOnAiWrite) and the owner leaves ai.edits.formatOnWrite on, and the
// write's result carries what the server then says of the file (diagnosticsOnAiWrite, ai.edits.diagnosticsOnWrite),
// waited for at most ai.edits.diagnosticsWaitMs. That is information, never a failure of the call: mid-way through an
// edit of several files the others are expected to be broken.

include "Tool.ch"

namespace Ai
{
	const int64 c_AfterWriteWaitMs = 1500;         // ai.edits.diagnosticsWaitMs's default
	const int64 c_AfterWriteMostWaitMs = 10000;
	const int64 c_AfterWriteMostItems = 8;         // diagnostics a result names

	class AfterWrite
	{
		public bool m_Format;
		public bool m_Diagnostics;
		public Core::String m_Label;     // the server's ("C4LSP")
		public int64 m_WaitMs;
		public int64 m_Mark;             // the file's diagnostics published before the write
	}

	// What follows a write of `path` by `call`: nothing for a file no server serves, or what its plugin does not ask
	// for or the owner turned off. Taken before the write. Without the server lock.
	AfterWrite AfterWriteOf(const ToolInvocation& call, Core::StringView path);
	// The indentation `text` uses for formatting it: tabs (the default) or spaces, and how wide.
	void IndentationOf(Core::StringView text, int64& tabSize, bool& insertSpaces);
	// What the result says of LspDiagnosticsAfter's `answer` (JSON) for the file: its errors and warnings line by line,
	// the other files with problems, or that the server had nothing yet.
	Core::String DiagnosticsSaid(Core::StringView answer, int64 waitMs);
}
