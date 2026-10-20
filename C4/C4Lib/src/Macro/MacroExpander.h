#pragma once
#include "MacroTable.h"
#include <set>
#include <string>
#include <vector>

namespace Scripting
{
	/// Where macro uses are expanded: `__FILE__` is the file's name and `__LINE__` the line.
	struct ExpansionSite
	{
		std::string file;
		unsigned line = 1;
	};

	struct ExpansionResult
	{
		std::string text;
		std::vector<std::string> errors; // e.g. a call with the wrong number of arguments
	};

	/// The error in a macro body (`##` at either end, `#` before a name that is not a parameter), or empty.
	std::string MacroBodyError(const MacroDefinition& def);

	/// Expands macro uses in C4 source text (docs/language/02). Object-like macros expand at any identifier,
	/// function-like ones when `(` follows. Literals and comments are copied as written. A parameter becomes
	/// its argument expanded first, except as an operand of `#` (stringize) or `##` (paste); the result is
	/// rescanned with the macro's own name left alone, so a macro never expands inside itself.
	class MacroExpander
	{
	public:
		static ExpansionResult Expand(const std::string& text, const MacroTable& table, const ExpansionSite& site);
		/// `text` ends inside the arguments of a function-like macro call: the call continues on the next line.
		static bool EndsInsideCall(const std::string& text, const MacroTable& table);

	private:
		MacroExpander(const MacroTable& table, const ExpansionSite& site) : m_Table(table), m_Site(site) {}

		void ExpandInto(const std::string& src, std::string& out);
		size_t ExpandIdentifier(const std::string& src, size_t start, size_t end, std::string& out);
		size_t ExpandCall(const MacroDefinition& def, const std::string& src, size_t start, size_t open, std::string& out);
		std::string Substitute(const MacroDefinition& def, const std::vector<std::string>& args);
		std::string Rescanned(const std::string& name, const std::string& text);
		std::string ExpandedArgument(const std::string& argument);

		const MacroTable& m_Table;
		const ExpansionSite& m_Site;
		std::set<std::string> m_Expanding;
		std::vector<std::string> m_Errors;
	};
}
