#include "Macro/MacroText.h"
#include <cctype>

namespace Scripting::MacroText
{
	bool IsIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
	bool IsIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
	bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

	std::string Trim(std::string_view s)
	{
		const size_t b = s.find_first_not_of(" \t\r\n");
		if (b == std::string_view::npos) return "";
		const size_t e = s.find_last_not_of(" \t\r\n");
		return std::string(s.substr(b, e - b + 1));
	}

	size_t IdentifierEnd(std::string_view s, size_t i)
	{
		while (i < s.size() && IsIdentChar(s[i])) ++i;
		return i;
	}

	size_t SkipSpaces(std::string_view s, size_t i)
	{
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
		return i;
	}

	bool StartsLiteral(std::string_view s, size_t i)
	{
		const auto quoteAt = [&](size_t k) { return k < s.size() && (s[k] == '"' || s[k] == '\''); };
		if (quoteAt(i)) return true;
		if (i > 0 && IsIdentChar(s[i - 1])) return false; // `menu"` is not a prefix
		if (i + 1 < s.size() && s[i] == 'u' && s[i + 1] == '8') return quoteAt(i + 2);
		return (i < s.size() && (s[i] == 'u' || s[i] == 'U')) && quoteAt(i + 1);
	}

	size_t LiteralEnd(std::string_view s, size_t i)
	{
		while (i < s.size() && s[i] != '"' && s[i] != '\'') ++i; // past the prefix
		if (i >= s.size()) return i;
		const char quote = s[i];
		for (++i; i < s.size() && s[i] != '\n'; ++i)
		{
			if (s[i] == '\\') { ++i; continue; }
			if (s[i] == quote) return i + 1;
		}
		return i;
	}

	bool StartsComment(std::string_view s, size_t i)
	{
		return i + 1 < s.size() && s[i] == '/' && (s[i + 1] == '/' || s[i + 1] == '*');
	}

	size_t CommentEnd(std::string_view s, size_t i)
	{
		if (s[i + 1] == '/')
		{
			const size_t eol = s.find('\n', i);
			return eol == std::string_view::npos ? s.size() : eol;
		}
		const size_t close = s.find("*/", i + 2);
		return close == std::string_view::npos ? s.size() : close + 2;
	}

	Call ReadCall(std::string_view s, size_t open)
	{
		Call call;
		std::string current;
		int depth = 0;
		for (size_t i = open; i < s.size();)
		{
			if (StartsLiteral(s, i) || StartsComment(s, i))
			{
				const size_t end = StartsLiteral(s, i) ? LiteralEnd(s, i) : CommentEnd(s, i);
				current.append(s.substr(i, end - i));
				i = end;
				continue;
			}
			const char c = s[i++];
			if (c == '(' && depth++ == 0) continue;
			if (c == ')' && --depth == 0)
			{
				call.args.push_back(Trim(current));
				call.end = i;
				call.closed = true;
				return call;
			}
			if (c == ',' && depth == 1)
			{
				call.args.push_back(Trim(current));
				current.clear();
				continue;
			}
			current += c;
		}
		call.end = s.size();
		return call;
	}
}
