#include "MacroExpander.h"

namespace Scripting
{
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
		std::vector<MacroTokens> NormalizeArguments(const MacroDefinition& def, std::vector<MacroTokens> args)
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

		MacroToken Made(MacroToken::Kind kind, std::string spelling, bool spaceBefore)
		{
			MacroToken token;
			token.kind = kind;
			token.spelling = std::move(spelling);
			token.spaceBefore = spaceBefore;
			return token;
		}

		// `tokens` in place of the use that started with `first`: the use's space before it comes before them.
		void AppendInPlaceOf(const MacroToken& first, MacroTokens tokens, MacroTokens& out)
		{
			if (!tokens.empty()) tokens.front().spaceBefore = first.spaceBefore;
			for (MacroToken& token : tokens) out.push_back(std::move(token));
		}
	}

	TokenExpansion MacroExpander::Expand(const MacroTokens& tokens, const MacroTable& table, const ExpansionSite& site)
	{
		MacroExpander expander(table, site);
		TokenExpansion result;
		result.tokens.reserve(tokens.size());
		expander.ExpandInto(tokens, result.tokens);
		result.errors = std::move(expander.m_Errors);
		return result;
	}

	ExpansionResult MacroExpander::Expand(const std::string& text, const MacroTable& table, const ExpansionSite& site)
	{
		TokenExpansion expanded = Expand(ReadMacroTokens(text, MacroToken::kNotInFile), table, site);
		return { MacroTokenText(expanded.tokens), std::move(expanded.errors) };
	}

	MacroExpander::Call MacroExpander::ReadCall(const MacroTokens& tokens, size_t open)
	{
		Call call;
		MacroTokens current;
		int depth = 0;
		for (size_t at = open; at < tokens.size(); ++at)
		{
			const MacroToken& token = tokens[at];
			if (token.Is("(") && depth++ == 0) continue;
			if (token.Is(")") && --depth == 0)
			{
				call.args.push_back(std::move(current));
				call.end = at + 1;
				call.closed = true;
				return call;
			}
			if (token.Is(",") && depth == 1)
			{
				call.args.push_back(std::move(current));
				current.clear();
				continue;
			}
			current.push_back(token);
		}
		call.end = tokens.size();
		return call;
	}

	void MacroExpander::ExpandInto(const MacroTokens& in, MacroTokens& out)
	{
		for (size_t at = 0; at < in.size();)
		{
			if (in[at].IsIdentifier()) { at = ExpandUse(in, at, out); continue; }
			out.push_back(in[at++]);
		}
	}

	size_t MacroExpander::ExpandUse(const MacroTokens& in, size_t at, MacroTokens& out)
	{
		const MacroToken& name = in[at];
		const MacroDefinition* def = m_Table.Get(name.spelling);
		if (!def && Builtin(name, out)) return at + 1;
		if (!def || m_Expanding.count(name.spelling)) { out.push_back(name); return at + 1; }

		const size_t open = at + 1;
		const bool called = open < in.size() && in[open].Is("(");
		if (def->isFunctionLike)
		{
			if (!called) { out.push_back(name); return at + 1; }
			return ExpandCall(*def, in, at, open, out);
		}
		MacroTokens replacement = Rescanned(def->name, Substitute(*def, {}));
		// `#define CALL F` then `CALL(1)`: the replacement names a function-like macro the tokens after the use call.
		const MacroDefinition* target = replacement.size() == 1 && replacement[0].IsIdentifier() ? m_Table.Get(replacement[0].spelling) : nullptr;
		if (called && target && target->isFunctionLike && !m_Expanding.count(target->name))
			return ExpandCall(*target, in, at, open, out);
		AppendInPlaceOf(name, std::move(replacement), out);
		return at + 1;
	}

	size_t MacroExpander::ExpandCall(const MacroDefinition& def, const MacroTokens& in, size_t start, size_t open, MacroTokens& out)
	{
		Call call = ReadCall(in, open);
		if (!call.closed)
		{
			out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(start), in.begin() + static_cast<std::ptrdiff_t>(open)); // an unclosed call is left as written
			return open;
		}
		const std::vector<MacroTokens> args = NormalizeArguments(def, std::move(call.args));
		const std::string error = ArgumentCountError(def, args.size());
		if (!error.empty())
		{
			m_Errors.push_back(error);
			out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(start), in.begin() + static_cast<std::ptrdiff_t>(call.end));
			return call.end;
		}
		AppendInPlaceOf(in[start], Rescanned(def.name, Substitute(def, args)), out);
		return call.end;
	}

	MacroTokens MacroExpander::Rescanned(const std::string& name, const MacroTokens& tokens)
	{
		m_Expanding.insert(name);
		MacroTokens out;
		out.reserve(tokens.size());
		ExpandInto(tokens, out);
		m_Expanding.erase(name);
		return out;
	}

	MacroTokens MacroExpander::ExpandedArgument(const MacroTokens& argument)
	{
		MacroTokens out;
		out.reserve(argument.size());
		ExpandInto(argument, out);
		return out;
	}

	bool MacroExpander::Builtin(const MacroToken& name, MacroTokens& out) const
	{
		if (name.Is("__FILE__")) out.push_back(Made(MacroToken::Kind::Literal, Quoted(m_Site.file), name.spaceBefore));
		else if (name.Is("__LINE__")) out.push_back(Made(MacroToken::Kind::Number, std::to_string(m_Site.line), name.spaceBefore));
		else return false;
		return true;
	}
}
