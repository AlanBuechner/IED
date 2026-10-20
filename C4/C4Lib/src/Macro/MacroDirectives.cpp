#include "Macro/Preprocessor.h"
#include "Macro/MacroExpander.h"
#include "Macro/MacroExpression.h"
#include "Macro/MacroText.h"
#include <algorithm>

namespace Scripting
{
	using namespace MacroText;

	namespace
	{
		bool IsReservedName(const std::string& name)
		{
			return name == "defined" || name == "__FILE__" || name == "__LINE__";
		}

		// `(a, b, ...)` right after a macro name; `bodyStart` is one past the `)`. Returns an error or "".
		std::string ReadParameters(const std::string& rest, size_t open, const std::string& name,
			std::vector<std::string>& params, size_t& bodyStart)
		{
			const size_t close = rest.find(')', open);
			if (close == std::string::npos) return "the parameter list of '" + name + "' has no ')'";
			bodyStart = close + 1;
			const std::string list = Trim(std::string_view(rest).substr(open + 1, close - open - 1));
			if (list.empty()) return "";
			size_t start = 0;
			while (start <= list.size())
			{
				const size_t comma = list.find(',', start);
				const std::string param = Trim(std::string_view(list).substr(start, (comma == std::string::npos ? list.size() : comma) - start));
				const bool isName = !param.empty() && IsIdentStart(param[0]) && IdentifierEnd(param, 0) == param.size();
				if (!isName && param != "...") return "the parameters of '" + name + "' must be names separated by commas";
				if (!params.empty() && params.back() == "...") return "'...' must be the last parameter of '" + name + "'";
				if (std::find(params.begin(), params.end(), param) != params.end()) return "'" + param + "' is a parameter of '" + name + "' twice";
				params.push_back(param);
				if (comma == std::string::npos) break;
				start = comma + 1;
			}
			return "";
		}

		bool SameDefinition(const MacroDefinition& a, const MacroDefinition& b)
		{
			return a.isFunctionLike == b.isFunctionLike && a.params == b.params && a.body == b.body;
		}
	}

	void Preprocessor::Directive(const std::string& line, unsigned number)
	{
		const std::string text = DirectiveText(line);
		const size_t nameStart = SkipSpaces(text, text.find('#') + 1);
		const size_t nameEnd = IdentifierEnd(text, nameStart);
		const std::string name = text.substr(nameStart, nameEnd - nameStart);
		const std::string rest = Trim(std::string_view(text).substr(nameEnd));
		m_Out += '\n';
		if (ConditionalDirective(name, rest, number) || !m_Conditions.Active())
			return;
		if (name.empty())
		{
			if (!rest.empty()) Report(number, ErrorSeverity::Error, "a '#' line needs a directive name");
		}
		else if (name == "define") Define(rest, number);
		else if (name == "undef") Undef(rest, number);
		else if (name == "error") Report(number, ErrorSeverity::Error, rest.empty() ? "#error" : "#error " + rest);
		else if (name == "warning") Report(number, ErrorSeverity::Warning, rest.empty() ? "#warning" : "#warning " + rest);
		else if (name == "pragma") {}
		else if (name == "line") Report(number, ErrorSeverity::Error, "#line is not supported: C4 keeps each file's own line numbers");
		else if (name == "include") Report(number, ErrorSeverity::Error, "'#include' is not a C4 directive: write include \"file.ch\"");
		else Report(number, ErrorSeverity::Error, "unknown directive '#" + name + "'");
	}

	// The directive without its comments; a /* left open continues the comment on the next line.
	std::string Preprocessor::DirectiveText(const std::string& line)
	{
		std::string out;
		for (size_t i = 0; i < line.size();)
		{
			if (StartsLiteral(line, i)) { const size_t end = LiteralEnd(line, i); out.append(line, i, end - i); i = end; continue; }
			if (line.compare(i, 2, "//") == 0) break;
			if (line.compare(i, 2, "/*") == 0)
			{
				const size_t close = line.find("*/", i + 2);
				if (close == std::string::npos) { m_InComment = true; break; }
				out += ' ';
				i = close + 2;
				continue;
			}
			out += line[i++];
		}
		return out;
	}

	bool Preprocessor::ConditionalDirective(const std::string& name, const std::string& rest, unsigned number)
	{
		const bool live = m_Conditions.Active();
		if (name == "if")
			m_Conditions.Open(live && Condition(rest, number), number);
		else if (name == "ifdef" || name == "ifndef")
		{
			const bool defined = live && m_Table.Has(MacroName(name, rest, number));
			m_Conditions.Open(name == "ifdef" ? defined : !defined, number);
		}
		else if (name == "elif")
		{
			const bool condition = m_Conditions.ElifNeedsCondition() && Condition(rest, number);
			if (const std::string error = m_Conditions.Elif(condition); !error.empty()) Report(number, ErrorSeverity::Error, error);
		}
		else if (name == "else" || name == "endif")
		{
			if (!rest.empty()) Report(number, ErrorSeverity::Error, "unexpected '" + rest + "' after #" + name);
			const std::string error = name == "else" ? m_Conditions.Else() : m_Conditions.Endif();
			if (!error.empty()) Report(number, ErrorSeverity::Error, error);
		}
		else
			return false;
		return true;
	}

	bool Preprocessor::Condition(const std::string& expression, unsigned number)
	{
		const ConditionValue value = EvaluateCondition(expression, m_Table, { m_FileName, number });
		if (!value.Ok())
		{
			Report(number, ErrorSeverity::Error, value.error);
			return false;
		}
		return value.value != 0;
	}

	// The one macro name `#ifdef` / `#ifndef` / `#undef` takes.
	std::string Preprocessor::MacroName(const std::string& directive, const std::string& rest, unsigned number)
	{
		const size_t end = rest.empty() || !IsIdentStart(rest[0]) ? 0 : IdentifierEnd(rest, 0);
		if (end == 0)
		{
			Report(number, ErrorSeverity::Error, "#" + directive + " needs a macro name");
			return "";
		}
		if (end < rest.size())
			Report(number, ErrorSeverity::Error, "unexpected '" + Trim(std::string_view(rest).substr(end)) + "' after the macro name of #" + directive);
		return rest.substr(0, end);
	}

	void Preprocessor::Define(const std::string& rest, unsigned number)
	{
		const size_t nameEnd = rest.empty() || !IsIdentStart(rest[0]) ? 0 : IdentifierEnd(rest, 0);
		if (nameEnd == 0) return Report(number, ErrorSeverity::Error, "#define needs a macro name");
		MacroDefinition def;
		def.name = rest.substr(0, nameEnd);
		if (IsReservedName(def.name)) return Report(number, ErrorSeverity::Error, "'" + def.name + "' cannot be defined as a macro");

		size_t bodyStart = nameEnd;
		if (nameEnd < rest.size() && rest[nameEnd] == '(') // function-like only with no space before `(` (C)
		{
			def.isFunctionLike = true;
			const std::string error = ReadParameters(rest, nameEnd, def.name, def.params, bodyStart);
			if (!error.empty()) return Report(number, ErrorSeverity::Error, error);
		}
		else if (nameEnd < rest.size() && !IsSpace(rest[nameEnd]))
			return Report(number, ErrorSeverity::Error, "a space must follow the macro name '" + def.name + "'");
		def.body = Trim(std::string_view(rest).substr(bodyStart));
		if (const std::string error = MacroBodyError(def); !error.empty())
			return Report(number, ErrorSeverity::Error, error);

		if (const MacroDefinition* old = m_Table.Get(def.name); old && !SameDefinition(*old, def))
			Report(number, ErrorSeverity::Warning, "'" + def.name + "' is redefined: #undef it first");
		if (def.isFunctionLike) m_Table.DefineFunction(def.name, def.params, def.body);
		else m_Table.Define(def.name, def.body);
	}

	void Preprocessor::Undef(const std::string& rest, unsigned number)
	{
		const std::string name = MacroName("undef", rest, number);
		if (!name.empty()) m_Table.Undef(name);
	}
}
