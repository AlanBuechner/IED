#pragma once
#include "Core/Core.h"
#include <string>
#include <string_view>
#include <vector>

namespace Scripting
{
	/// A token of the macro pass (docs/language/02): what a use and its arguments are read as, what a macro's body is, and
	/// what a use expands to. Expansion works on these, never on text (R8).
	struct MacroToken
	{
		enum class Kind : uint8 { Identifier, Number, Literal, Punctuation };
		static constexpr uint32 kNotInFile = 0xFFFFFFFFu;

		Kind kind = Kind::Punctuation;
		std::string spelling;
		uint32 fileOffset = kNotInFile; // where the file spells it (the use, an argument); none for a body's or a pasted one
		bool spaceBefore = false;       // white space or a comment comes before it

		bool IsIdentifier() const { return kind == Kind::Identifier; }
		bool Is(std::string_view text) const { return spelling == text; }
	};

	using MacroTokens = std::vector<MacroToken>;

	/// `text` read as macro tokens: identifiers, numbers (`0xFF`, `1e+5` whole), string and character literals (with their
	/// `u8` / `u` / `U` prefixes), `##`, and every other character alone; a comment is white space. A token's file offset is
	/// `fileOffset` plus its place in `text` (kNotInFile: none).
	MacroTokens ReadMacroTokens(std::string_view text, uint32 fileOffset);

	/// The tokens as text: their spellings, one space where white space came before.
	std::string MacroTokenText(const MacroTokens& tokens);
	/// What the spelling of a token made by `##` is: an identifier, a number, a literal or punctuation.
	MacroToken::Kind KindOfSpelling(std::string_view spelling);
	/// Whether a number starts at `at` (a digit, or `.` and a digit), and one past it: its letters are no names (`0xFF`).
	bool StartsMacroNumber(std::string_view text, size_t at);
	size_t MacroNumberEnd(std::string_view text, size_t at);
}
