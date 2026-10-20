#include "Target/Expression.h"
#include <cctype>

namespace C4DAP::Target
{
	namespace
	{
		struct Cursor
		{
			const std::string& text;
			size_t at = 0;

			void SkipSpace() { while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at; }
			bool Done() { SkipSpace(); return at >= text.size(); }
			bool Take(const char* token)
			{
				SkipSpace();
				const std::string want = token;
				if (text.compare(at, want.size(), want) != 0) return false;
				at += want.size();
				return true;
			}
			std::string Identifier()
			{
				SkipSpace();
				const size_t start = at;
				if (at < text.size() && (std::isalpha(static_cast<unsigned char>(text[at])) || text[at] == '_'))
					while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '_')) ++at;
				return text.substr(start, at - start);
			}
			std::optional<uint64_t> Number()
			{
				SkipSpace();
				uint64_t value = 0;
				const size_t start = at;
				while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])))
				{
					const uint64_t digit = static_cast<uint64_t>(text[at++] - '0');
					if (value > (UINT64_MAX - digit) / 10) return std::nullopt;
					value = value * 10 + digit;
				}
				if (at == start) return std::nullopt;
				return value;
			}
		};

		// `name` or `Ns::Type::name`.
		std::string ScopedName(Cursor& cursor)
		{
			std::string name = cursor.Identifier();
			while (!name.empty())
			{
				const size_t mark = cursor.at;
				if (!cursor.Take("::")) break;
				const std::string part = cursor.Identifier();
				if (part.empty()) { cursor.at = mark; break; }
				name += "::" + part;
			}
			return name;
		}

		std::optional<PathStep> NextStep(Cursor& cursor, std::string& error)
		{
			PathStep step;
			if (cursor.Take("->")) step.kind = PathStep::Kind::PointerField;
			else if (cursor.Take(".")) step.kind = PathStep::Kind::Field;
			else if (cursor.Take("["))
			{
				const std::optional<uint64_t> index = cursor.Number();
				if (!index || !cursor.Take("]")) { error = "an index is a number in [ ]"; return std::nullopt; }
				step.kind = PathStep::Kind::Index;
				step.index = *index;
				return step;
			}
			else { error = "unexpected text at column " + std::to_string(cursor.at + 1); return std::nullopt; }
			step.field = cursor.Identifier();
			if (step.field.empty()) { error = "a field name must follow . or ->"; return std::nullopt; }
			return step;
		}
	}

	std::optional<Expression> ParseExpression(const std::string& text, std::string& error)
	{
		Cursor cursor{ text };
		Expression expression;
		while (cursor.Take("*")) ++expression.dereferences;
		expression.variable = ScopedName(cursor);
		if (expression.variable.empty())
		{
			error = "an expression starts with a variable's name";
			return std::nullopt;
		}
		while (!cursor.Done())
		{
			std::optional<PathStep> step = NextStep(cursor, error);
			if (!step) return std::nullopt;
			expression.path.push_back(*step);
		}
		return expression;
	}
} // namespace C4DAP::Target
