#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Scripting::MacroText
{
	bool IsIdentStart(char c);
	bool IsIdentChar(char c);
	bool IsSpace(char c);
	std::string Trim(std::string_view s);

	/// The end of the identifier starting at `i`.
	size_t IdentifierEnd(std::string_view s, size_t i);
	/// The first index at or after `i` that is not a space or tab.
	size_t SkipSpaces(std::string_view s, size_t i);

	/// A string or character literal starts at `i` (a quote, or a `u8` / `u` / `U` prefix and a quote).
	bool StartsLiteral(std::string_view s, size_t i);
	/// One past the literal that starts at `i` (its closing quote, or the end of the line when it has none).
	size_t LiteralEnd(std::string_view s, size_t i);
	/// A `//` or `/*` comment starts at `i`.
	bool StartsComment(std::string_view s, size_t i);
	/// One past the comment that starts at `i`; a `/*` with no `*/` runs to the end of `s`.
	size_t CommentEnd(std::string_view s, size_t i);

	/// The arguments of a call whose `(` is at `open`, split at top-level commas (literals and nested
	/// parentheses kept whole); `closed` is false when `s` ends first.
	struct Call
	{
		std::vector<std::string> args;
		size_t end = 0; // one past the `)`
		bool closed = false;
	};
	Call ReadCall(std::string_view s, size_t open);
}
