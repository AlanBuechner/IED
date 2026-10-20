#pragma once
#include "MacroTable.h"
#include "Macro/MacroDiagnostic.h"
#include <functional>
#include <string>
#include <vector>

namespace Scripting
{
	/// A file's macro-expanded text and the errors and warnings of its directives.
	struct PreprocessedFile
	{
		std::string text;
		std::vector<MacroDiagnostic> diagnostics;
	};

	/// The directive processor (docs/language/02). Given one file's text and a running MacroTable it runs
	/// #define / #undef / #if... / #error / #warning / #pragma, expands macro uses in live code, and calls
	/// `inheritMacros(path)` at each `include "path"` line so the caller can merge that file's macros
	/// before the lines after it expand. Comments and string literals are respected across lines, a
	/// directive continues on the next line after a trailing `\`, and every line of the input stays one
	/// line of the output, so locations map back unchanged. The table ends as the file's macro state.
	class MacroPreprocessor
	{
	public:
		using InheritCallback = std::function<void(const std::string& includePath)>;

		/// `fileName` is what `__FILE__` expands to.
		static PreprocessedFile Process(const std::string& text, const std::string& fileName, MacroTable& table,
			const InheritCallback& inheritMacros);
	};
}
