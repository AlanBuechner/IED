#pragma once
#include "Core/Core.h"
#include <string>
#include <vector>
// TextEdit is a by-value return type of Format, so the full definition must be visible to callers.
#include "Protocol/LspTypes.h"

namespace C4LSP::Features
{
	/// Conservative C4 formatter. It normalizes whitespace ONLY - indentation, spacing around
	/// tokens and trailing whitespace - and never touches token text or ordering, so it cannot
	/// change a program's meaning even when the document has parse errors: it works on the LEXER
	/// stream (the same reason semantic tokens re-lex editor text), not the AST. The rules mirror
	/// what Visual Assist X applies in auto-format mode for C++-like code and match the C4 house
	/// style seen across this repository: 4-space indent per block level, single spaces around
	/// binary operators (= + - / % == != <= >=), no space after '(' or before ')', generic
	/// parameters glued (`Vector<int32>`, `HashMap<int32, int32>` - so '<' and '>' are deliberately
	/// left alone to protect templates), pointer stars glued to their type (`int32*` - '*' is left
	/// alone for the same reason), '.' / '::' / '->' glue with no surrounding space. Gaps that
	/// contain a comment or cross a line boundary are never touched, and blank lines collapse to
	/// empty. Every edit lands strictly between adjacent tokens (or in a line's leading whitespace),
	/// so even a garbage file can only have its whitespace changed. Indentation always follows the
/// client options (`insertSpaces` / `tabSize`) exactly, as LSP requires - no style guessing.
	class Formatting
	{
	public:
		struct Options { int tabSize = 4; bool insertSpaces = true; };

		static std::vector<Protocol::TextEdit> Format(const std::string& text, const Options& options);
	};
} // namespace C4LSP::Features
