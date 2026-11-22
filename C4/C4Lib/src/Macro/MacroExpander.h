#pragma once
#include "MacroTable.h"
#include "Macro/MacroToken.h"
#include <set>
#include <string>
#include <vector>

namespace C4Compiler
{
	/// Where macro uses are expanded: `__FILE__` is the file's name and `__LINE__` the line.
	struct ExpansionSite
	{
		std::string file;
		unsigned line = 1;
	};

	/// What tokens expand to.
	struct TokenExpansion
	{
		MacroTokens tokens;
		std::vector<std::string> errors; // e.g. a call with the wrong number of arguments
	};

	/// What text expands to (a `#if` condition, a tool's question).
	struct ExpansionResult
	{
		std::string text;
		std::vector<std::string> errors;
	};

	/// The error in a macro body (`##` at either end, `#` before a name that is not a parameter), or empty.
	std::string MacroBodyError(const MacroDefinition& def);

	/// Expands macro uses over tokens (docs/language/02, R8). Object-like macros expand at any identifier, function-like ones
	/// when `(` follows. A parameter becomes its argument expanded first, except as an operand of `#` (stringize) or `##`
	/// (paste); the result is rescanned with the macro's own name left alone, so a macro never expands inside itself. A token
	/// keeps where the file spells it (an argument's); one a body or `##` made has no place in the file.
	class MacroExpander
	{
	public:
		static TokenExpansion Expand(const MacroTokens& tokens, const MacroTable& table, const ExpansionSite& site);
		/// `text` read as tokens, expanded, written back as text (comments are white space).
		static ExpansionResult Expand(const std::string& text, const MacroTable& table, const ExpansionSite& site);

		/// The arguments of a call whose `(` is `tokens[open]`, split at its top-level commas; `closed` is false when the
		/// tokens end first.
		struct Call
		{
			std::vector<MacroTokens> args;
			size_t end = 0; // one past the `)`
			bool closed = false;
		};
		static Call ReadCall(const MacroTokens& tokens, size_t open);

	private:
		MacroExpander(const MacroTable& table, const ExpansionSite& site) : m_Table(table), m_Site(site) {}

		void ExpandInto(const MacroTokens& in, MacroTokens& out);
		// The use whose name is `in[at]`, expanded into `out`; returns the index after it.
		size_t ExpandUse(const MacroTokens& in, size_t at, MacroTokens& out);
		size_t ExpandCall(const MacroDefinition& def, const MacroTokens& in, size_t start, size_t open, MacroTokens& out);
		MacroTokens Substitute(const MacroDefinition& def, const std::vector<MacroTokens>& args);
		MacroTokens Rescanned(const std::string& name, const MacroTokens& tokens);
		MacroTokens ExpandedArgument(const MacroTokens& argument);
		// `__FILE__` / `__LINE__` when `name` is one (and no macro of its own); false otherwise.
		bool Builtin(const MacroToken& name, MacroTokens& out) const;

		const MacroTable& m_Table;
		const ExpansionSite& m_Site;
		std::set<std::string> m_Expanding;
		std::vector<std::string> m_Errors;
	};
}
