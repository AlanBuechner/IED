#pragma once
#include "MacroTable.h"
#include "Macro/MacroDiagnostic.h"
#include "Macro/MacroLayout.h"
#include <functional>
#include <string>
#include <vector>

namespace Scripting
{
	/// What the macro pass made of a file: where its lines are left out and its uses expanded, and the errors and warnings
	/// of its directives and uses.
	struct PreprocessedFile
	{
		Ref<MacroLayout> layout;
		std::vector<MacroDiagnostic> diagnostics;
	};

	/// The directive processor (docs/language/02). Given one file's text and a running MacroTable it runs #define / #undef /
	/// #if... / #error / #warning / #pragma, expands macro uses in live code over tokens, and calls `inheritMacros(path)` at
	/// each `include "path"` line so the caller can merge that file's macros before the lines after it expand. Comments and
	/// string literals are respected across lines, and a directive continues on the next line after a trailing `\`. The
	/// file's text is never rewritten: the result says which bytes are left out and which uses read as their expansion
	/// (R8). The table ends as the file's macro state.
	class MacroPreprocessor
	{
	public:
		using InheritCallback = std::function<void(const std::string& includePath)>;

		/// `fileName` is what `__FILE__` expands to.
		static PreprocessedFile Process(const std::string& text, const std::string& fileName, MacroTable& table,
			const InheritCallback& inheritMacros);
	};
}
