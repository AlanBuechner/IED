#include "MacroExpander.h"

namespace C4Compiler
{
	namespace
	{
		constexpr size_t kNoParam = static_cast<size_t>(-1);

		// A macro body read as tokens: a token as written, a parameter use, `#param` and `##`.
		struct BodyToken
		{
			enum class Kind { Text, Param, Stringize, Paste } kind = Kind::Text;
			MacroToken token;        // Text: the token; Param / Stringize: the parameter's name (its space before)
			size_t param = kNoParam;
		};

		size_t ParamIndex(const MacroDefinition& def, const std::string& name)
		{
			for (size_t p = 0; p < def.params.size(); ++p)
				if (def.params[p] == name) return p;
			return def.IsVariadic() && name == "__VA_ARGS__" ? def.params.size() - 1 : kNoParam;
		}

		std::vector<BodyToken> BodyTokens(const MacroDefinition& def)
		{
			using Kind = BodyToken::Kind;
			const MacroTokens tokens = ReadMacroTokens(def.body, MacroToken::kNotInFile);
			std::vector<BodyToken> body;
			body.reserve(tokens.size());
			for (size_t at = 0; at < tokens.size(); ++at)
			{
				const MacroToken& token = tokens[at];
				if (token.Is("##")) { body.push_back({ Kind::Paste, token }); continue; }
				if (token.Is("#") && def.isFunctionLike && at + 1 < tokens.size() && tokens[at + 1].IsIdentifier())
				{
					if (const size_t param = ParamIndex(def, tokens[at + 1].spelling); param != kNoParam)
					{
						body.push_back({ Kind::Stringize, token, param });
						++at;
						continue;
					}
				}
				const size_t param = token.IsIdentifier() ? ParamIndex(def, token.spelling) : kNoParam;
				body.push_back({ param == kNoParam ? Kind::Text : Kind::Param, token, param });
			}
			return body;
		}

		// Named parameters before `...` take the leading arguments, those after it the trailing ones, and `__VA_ARGS__` the
		// arguments between, joined with ", ".
		MacroTokens ArgumentFor(const MacroDefinition& def, const std::vector<MacroTokens>& args, size_t param)
		{
			if (!def.IsVariadic() || param < def.params.size() - 1) return param < args.size() ? args[param] : MacroTokens();
			MacroTokens joined;
			for (size_t a = def.params.size() - 1; a < args.size(); ++a)
			{
				if (a > def.params.size() - 1)
				{
					MacroToken comma;
					comma.spelling = ",";
					joined.push_back(comma);
				}
				for (size_t t = 0; t < args[a].size(); ++t)
				{
					joined.push_back(args[a][t]);
					if (t == 0) joined.back().spaceBefore = a > def.params.size() - 1;
				}
			}
			return joined;
		}

		// `#x`: the argument as written, white space between its tokens one space, quotes and backslashes in literals escaped.
		std::string Stringized(const MacroTokens& argument)
		{
			std::string out = "\"";
			for (size_t at = 0; at < argument.size(); ++at)
			{
				if (at > 0 && argument[at].spaceBefore) out += ' ';
				for (char c : argument[at].spelling)
				{
					if (argument[at].kind == MacroToken::Kind::Literal && (c == '"' || c == '\\')) out += '\\';
					out += c;
				}
			}
			return out + "\"";
		}

		bool NextToPaste(const std::vector<BodyToken>& body, size_t at)
		{
			return (at > 0 && body[at - 1].kind == BodyToken::Kind::Paste) || (at + 1 < body.size() && body[at + 1].kind == BodyToken::Kind::Paste);
		}

		// What one operand of the body puts out, pasted onto what came before when a `##` precedes it: the spellings of the
		// left's last token and the right's first are one token, which has no place in the file. An empty operand pastes
		// nothing (C's placemarker).
		class Output
		{
		public:
			void Paste() { m_Paste = true; }

			void Append(MacroTokens tokens)
			{
				const bool chained = m_Paste;
				m_Paste = false;
				const bool paste = chained && !m_LastEmpty && !m_Out.empty() && !tokens.empty();
				// What a paste chain holds so far is empty only when every operand of it was (`a ## EMPTY ## b` is `ab`).
				m_LastEmpty = chained ? m_LastEmpty && tokens.empty() : tokens.empty();
				size_t at = 0;
				if (paste)
				{
					MacroToken& left = m_Out.back();
					left.spelling += tokens[0].spelling;
					left.kind = KindOfSpelling(left.spelling);
					left.fileOffset = MacroToken::kNotInFile;
					at = 1;
				}
				for (; at < tokens.size(); ++at) m_Out.push_back(std::move(tokens[at]));
			}

			MacroTokens Take() { return std::move(m_Out); }

		private:
			MacroTokens m_Out;
			bool m_Paste = false;
			bool m_LastEmpty = false;
		};
	}

	MacroTokens MacroExpander::Substitute(const MacroDefinition& def, const std::vector<MacroTokens>& args)
	{
		using Kind = BodyToken::Kind;
		const std::vector<BodyToken> body = BodyTokens(def);
		Output out;
		for (size_t at = 0; at < body.size(); ++at)
		{
			const BodyToken& part = body[at];
			switch (part.kind)
			{
			case Kind::Paste:
				out.Paste();
				break;
			case Kind::Text:
				out.Append({ part.token });
				break;
			case Kind::Stringize:
			{
				MacroToken literal;
				literal.kind = MacroToken::Kind::Literal;
				literal.spelling = Stringized(ArgumentFor(def, args, part.param));
				literal.spaceBefore = part.token.spaceBefore;
				out.Append({ literal });
				break;
			}
			case Kind::Param:
			{
				MacroTokens argument = NextToPaste(body, at) ? ArgumentFor(def, args, part.param) : ExpandedArgument(ArgumentFor(def, args, part.param));
				if (!argument.empty()) argument.front().spaceBefore = part.token.spaceBefore;
				out.Append(std::move(argument));
				break;
			}
			}
		}
		return out.Take();
	}

	std::string MacroBodyError(const MacroDefinition& def)
	{
		using Kind = BodyToken::Kind;
		const std::vector<BodyToken> body = BodyTokens(def);
		if (!body.empty() && (body.front().kind == Kind::Paste || body.back().kind == Kind::Paste))
			return "'##' cannot start or end the body of the macro '" + def.name + "'";
		for (const BodyToken& part : body)
			if (def.isFunctionLike && part.kind == Kind::Text && part.token.Is("#"))
				return "'#' in the macro '" + def.name + "' must be followed by one of its parameters";
		return "";
	}
}
