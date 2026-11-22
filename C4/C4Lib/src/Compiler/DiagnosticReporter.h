#pragma once
#include "Compiler/CompilationResult.h"
#include <string>

namespace C4Compiler
{
	class CompilerInstance;
	class SourceManager;

	/// Renders a CompilationResult to stdout: one formatted block per diagnostic (with source
	/// context and caret when a location is available) followed by the summary footer.
	///
	/// Presentation only - it never mutates the result or the compiler instance, so both the
	/// CLI driver and (later) an LSP host can reuse the same diagnostics without side effects.
	class DiagnosticReporter
	{
	public:
		/// Print every diagnostic in `result` followed by the summary footer.
		static void Report(const CompilationResult& result, const CompilerInstance* compiler);

		/// One diagnostic as Report prints it: `file (line:column) severity: message`, then the source line and a
		/// caret under the column when a location is known, the macro the location came from when one did (R8), then a
		/// blank line.
		static std::string Format(const CompilationError& err, const CompilerInstance* compiler);

	private:
		/// Pretty-print a single diagnostic with optional source context and caret line.
		static void PrintOneError(const CompilationError& err, const CompilerInstance* compiler);

		/// `  note: in the expansion of the macro 'X' (file:line): <what it expands to>` for a location a macro use made;
		/// empty otherwise.
		static std::string ExpansionNote(const SourceLocation& location, const SourceManager& sources);

		/// Build the "Compilation Succeeded/Failed" footer with session stats and counts.
		static void PrintSummaryFooter(const CompilationResult& result, const CompilerInstance* compiler);
	};
}
