#pragma once
#include "Compiler/CompilationResult.h"

namespace Scripting
{
	class CompilerInstance;

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
		/// caret under the column when a location is known, then a blank line.
		static std::string Format(const CompilationError& err, const CompilerInstance* compiler);

	private:
		/// Pretty-print a single diagnostic with optional source context and caret line.
		static void PrintOneError(const CompilationError& err, const CompilerInstance* compiler);

		/// Build the "Compilation Succeeded/Failed" footer with session stats and counts.
		static void PrintSummaryFooter(const CompilationResult& result, const CompilerInstance* compiler);
	};
}
