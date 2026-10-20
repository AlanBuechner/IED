#include "Backend/HIR/AsmTemplate.h"

namespace Scripting::AsmTemplate
{
	namespace
	{
		using Result = QueryResult<std::string>;

		// x86 operand modifiers: 8-, 16-, 32-, 64-bit register; xmm, ymm, zmm; a bare constant.
		bool IsModifier(const std::string& m)
		{
			return m == "b" || m == "w" || m == "k" || m == "q" || m == "h" || m == "x" || m == "t" || m == "g" || m == "c";
		}

		// `{name}` or `{name:m}` starting at `open`: its LLVM reference, or why it is not one.
		Result Reference(const std::string& text, size_t open, size_t close, const std::function<int(const std::string&)>& indexOf)
		{
			const std::string inside = text.substr(open + 1, close - open - 1);
			const size_t colon = inside.find(':');
			const std::string name = inside.substr(0, colon);
			const std::string modifier = colon == std::string::npos ? "" : inside.substr(colon + 1);
			const int index = indexOf(name);
			if (index < 0)
				return Result::Fail(QueryError::NotFound, "the asm template names '{" + name + "}', which is not an operand: declare it with "
					"in(...), out(...), inout(...) or const");
			if (!modifier.empty() && !IsModifier(modifier))
				return Result::Fail(QueryError::NotAValue, "'" + modifier + "' in '{" + inside + "}' is not an x86 operand modifier: use b, w, k, "
					"q, h, x, t, g or c");
			return Result("${" + std::to_string(index) + (modifier.empty() ? "" : ":" + modifier) + "}");
		}

		Result Line(const std::string& line, const std::function<int(const std::string&)>& indexOf)
		{
			std::string out;
			for (size_t i = 0; i < line.size(); ++i)
			{
				const char c = line[i];
				if (c == '$') { out += "$$"; continue; }
				if (c == '}')
				{
					if (i + 1 < line.size() && line[i + 1] == '}') { out += '}'; ++i; continue; }
					return Result::Fail(QueryError::NotAValue, "a '}' in an asm template closes nothing: write '}}' for a brace");
				}
				if (c != '{') { out += c; continue; }
				if (i + 1 < line.size() && line[i + 1] == '{') { out += '{'; ++i; continue; }
				const size_t close = line.find('}', i);
				if (close == std::string::npos)
					return Result::Fail(QueryError::NotAValue, "a '{' in an asm template is not closed: name an operand as {name}, or write '{{'");
				auto reference = Reference(line, i, close, indexOf);
				if (!reference.Ok()) return reference;
				out += reference.value;
				i = close;
			}
			return Result(out);
		}
	}

	QueryResult<std::string> Translate(const std::vector<std::string>& lines, const std::function<int(const std::string&)>& indexOf)
	{
		std::string text;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			auto line = Line(lines[i], indexOf);
			if (!line.Ok()) return line;
			if (i > 0) text += "\n\t";
			text += line.value;
		}
		return Result(text);
	}
}
