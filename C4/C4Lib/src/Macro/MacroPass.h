#pragma once
#include "MacroTable.h"
#include "Macro/MacroDiagnostic.h"
#include "Macro/MacroLayout.h"
#include <functional>
#include <string>
#include <map>
#include <vector>

namespace Scripting
{
	/// Runs the macro preprocessor over a set of C4 source files, producing for each file where its text is left out and
	/// its uses expanded (its MacroLayout, R8) and its final effective MacroTable. Each file is processed once, with the
	/// macros it starts with plus what its includes export (docs/language/02).
	class MacroPass
	{
	public:
		struct FileResult
		{
			std::string name;    // display name (relative path within the owning root folder)
			std::string absPath; // normalized absolute path - the file's unique identity across roots
			Ref<MacroLayout> layout; // what the pass did to its text; null for a file it kept as it is (an interface file)
			MacroTable table;    // final effective macro state
			MacroTable exported; // what an includer receives: the table without the macros the file started with (its project's, D-59)
			std::vector<MacroDiagnostic> diagnostics; // the errors and warnings of its directives and uses

			/// The file's text as the pass leaves it: `written` read through its layout (MacroLayout::Render).
			std::string Text(std::string_view written) const;
		};

		/// One file for RunFiles.
		struct Input
		{
			std::string absPath; // the file's identity
			std::string name;    // display name
			std::string text;
			std::map<std::string, std::string> predefined; // the macros it starts with (its project's, D-59)
			bool expanded = false; // an interface file (docs/runtime/04): `text` is kept as it is, `exports` is what an includer gets
			MacroTable exports;
		};

		/// The absolute path of the file `include` names from the file at `includingAbsPath`; "" when none.
		using IncludeResolver = std::function<std::string(const std::string& includingAbsPath, const std::string& include)>;

		/// Preprocesses `files`. An include carries the macros of the file `resolve` names, when it is one of them.
		static std::vector<FileResult> RunFiles(std::vector<Input> files, const IncludeResolver& resolve);

		/// Write each result's macro table to <name>.c4macro next to the output dir.
		static void ExportTables(const std::vector<FileResult>& results, const std::string& outDir);

	private:
		struct Context
		{
			std::map<std::string, Input> files; // absolute path -> the file
			IncludeResolver resolve;
		};

		/// The result of `absPath`, made once: every include of it reuses it (BUG-239).
		static const FileResult& ProcessOne(const Context& ctx, const std::string& absPath,
			std::map<std::string, FileResult>& done, std::vector<std::string>& inProgress);
	};
}
