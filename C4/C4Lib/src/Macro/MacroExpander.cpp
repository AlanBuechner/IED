#include "MacroExpander.h"
#include "Macro/MacroText.h"

namespace Scripting
{
	using namespace MacroText;

	namespace
	{
		std::string Quoted(const std::string& text)
		{
			std::string out = "\"";
			for (char c : text)
			{
				if (c == '"' || c == '\\') out += '\\';
				out += c;
			}
			return out + "\"";
		}

		// `Z()` passes no arguments; a variadic call may end in an empty argument (`CALL(9,)`).
		std::vector<std::string> NormalizeArguments(const MacroDefinition& def, std::vector<std::string> args)
		{
			if (args.size() == 1 && args[0].empty() && def.params.empty()) args.clear();
			if (def.IsVariadic() && args.size() > 1 && args.back().empty()) args.pop_back();
			return args;
		}

		std::string ArgumentCountError(const MacroDefinition& def, size_t given)
		{
			const size_t named = def.IsVariadic() ? def.params.size() - 1 : def.params.size();
			const bool fits = def.IsVariadic() ? given >= named : (given == named || (named == 1 && given == 0));
			if (fits) return "";
			const std::string wanted = (def.IsVariadic() ? "at least " : "") + std::to_string(named);
			return "the macro '" + def.name + "' takes " + wanted + " argument" + (named == 1 ? "" : "s") +
				", but " + std::to_string(given) + (given == 1 ? " was" : " were") + " given";
		}
	}

	ExpansionResult MacroExpander::Expand(const std::string& text, const MacroTable& table, const ExpansionSite& site)
	{
		MacroExpander expander(table, site);
		ExpansionResult result;
		result.text.reserve(text.size());
		expander.ExpandInto(text, result.text);
		result.errors = std::move(expander.m_Errors);
		return result;
	}

	bool MacroExpander::EndsInsideCall(const std::string& text, const MacroTable& table)
	{
		for (size_t i = 0; i < text.size();)
		{
			if (StartsLiteral(text, i)) { i = LiteralEnd(text, i); continue; }
			if (StartsComment(text, i)) { i = CommentEnd(text, i); continue; }
			if (!IsIdentStart(text[i])) { ++i; continue; }
			const size_t end = IdentifierEnd(text, i);
			const MacroDefinition* def = table.Get(text.substr(i, end - i));
			const size_t open = SkipSpaces(text, end);
			if (def && def->isFunctionLike && open < text.size() && text[open] == '(')
			{
				const Call call = ReadCall(text, open);
				if (!call.closed) return true;
				i = call.end;
				continue;
			}
			i = end;
		}
		return false;
	}

	void MacroExpander::ExpandInto(const std::string& src, std::string& out)
	{
		for (size_t i = 0; i < src.size();)
		{
			if (StartsLiteral(src, i) || StartsComment(src, i))
			{
				const size_t end = StartsLiteral(src, i) ? LiteralEnd(src, i) : CommentEnd(src, i);
				out.append(src, i, end - i);
				i = end;
				continue;
			}
			if (IsIdentStart(src[i]))
			{
				i = ExpandIdentifier(src, i, IdentifierEnd(src, i), out);
				continue;
			}
			out += src[i++];
		}
	}

	size_t MacroExpander::ExpandIdentifier(const std::string& src, size_t start, size_t end, std::string& out)
	{
		const std::string name = src.substr(start, end - start);
		const MacroDefinition* def = m_Table.Get(name);
		if (!def && name == "__FILE__") { out += Quoted(m_Site.file); return end; }
		if (!def && name == "__LINE__") { out += std::to_string(m_Site.line); return end; }
		if (!def || m_Expanding.count(name)) { out += name; return end; }

		const size_t open = SkipSpaces(src, end);
		const bool called = open < src.size() && src[open] == '(';
		if (def->isFunctionLike)
		{
			if (!called) { out += name; return end; }
			return ExpandCall(*def, src, start, open, out);
		}
		const std::string replacement = Rescanned(name, Substitute(*def, {}));
		// `#define CALL F` then `CALL(1)`: the replacement names a function-like macro the rest of the line calls.
		const MacroDefinition* target = m_Table.Get(Trim(replacement));
		if (called && target && target->isFunctionLike && !m_Expanding.count(target->name))
			return ExpandCall(*target, src, start, open, out);
		out += replacement;
		return end;
	}

	size_t MacroExpander::ExpandCall(const MacroDefinition& def, const std::string& src, size_t start, size_t open, std::string& out)
	{
		const Call call = ReadCall(src, open);
		if (!call.closed)
		{
			out.append(src, start, open - start); // an unclosed call is left as written
			return open;
		}
		const std::vector<std::string> args = NormalizeArguments(def, call.args);
		const std::string error = ArgumentCountError(def, args.size());
		if (!error.empty())
		{
			m_Errors.push_back(error);
			out.append(src, start, call.end - start);
			return call.end;
		}
		out += Rescanned(def.name, Substitute(def, args));
		return call.end;
	}

	std::string MacroExpander::Rescanned(const std::string& name, const std::string& text)
	{
		m_Expanding.insert(name);
		std::string out;
		out.reserve(text.size());
		ExpandInto(text, out);
		m_Expanding.erase(name);
		return out;
	}

	std::string MacroExpander::ExpandedArgument(const std::string& argument)
	{
		std::string out;
		out.reserve(argument.size());
		ExpandInto(argument, out);
		return out;
	}
}
