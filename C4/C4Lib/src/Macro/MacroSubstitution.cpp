#include "MacroExpander.h"
#include "Macro/MacroText.h"

namespace Scripting
{
	using namespace MacroText;

	namespace
	{
		constexpr size_t kNoParam = static_cast<size_t>(-1);

		// A macro body read as tokens: text kept as written, a parameter use, `#param` and `##`.
		struct BodyToken
		{
			enum class Kind { Text, Space, Param, Stringize, Paste } kind = Kind::Text;
			std::string text;
			size_t param = kNoParam;
		};

		size_t ParamIndex(const MacroDefinition& def, std::string_view name)
		{
			for (size_t p = 0; p < def.params.size(); ++p)
				if (def.params[p] == name) return p;
			return def.IsVariadic() && name == "__VA_ARGS__" ? def.params.size() - 1 : kNoParam;
		}

		std::vector<BodyToken> BodyTokens(const MacroDefinition& def)
		{
			using Kind = BodyToken::Kind;
			const std::string& body = def.body;
			std::vector<BodyToken> tokens;
			for (size_t i = 0; i < body.size();)
			{
				if (StartsLiteral(body, i) || StartsComment(body, i))
				{
					const size_t end = StartsLiteral(body, i) ? LiteralEnd(body, i) : CommentEnd(body, i);
					tokens.push_back({ Kind::Text, body.substr(i, end - i) });
					i = end;
				}
				else if (IsSpace(body[i]))
				{
					size_t end = i;
					while (end < body.size() && IsSpace(body[end])) ++end;
					tokens.push_back({ Kind::Space, body.substr(i, end - i) });
					i = end;
				}
				else if (body.compare(i, 2, "##") == 0)
				{
					tokens.push_back({ Kind::Paste, "##" });
					i += 2;
				}
				else if (body[i] == '#' && def.isFunctionLike)
				{
					const size_t nameStart = SkipSpaces(body, i + 1);
					const size_t nameEnd = IdentifierEnd(body, nameStart);
					const size_t param = ParamIndex(def, std::string_view(body).substr(nameStart, nameEnd - nameStart));
					tokens.push_back(param == kNoParam ? BodyToken{ Kind::Text, "#" } : BodyToken{ Kind::Stringize, "", param });
					i = param == kNoParam ? i + 1 : nameEnd;
				}
				else if (IsIdentStart(body[i]))
				{
					const size_t end = IdentifierEnd(body, i);
					const size_t param = ParamIndex(def, std::string_view(body).substr(i, end - i));
					tokens.push_back(param == kNoParam ? BodyToken{ Kind::Text, body.substr(i, end - i) } : BodyToken{ Kind::Param, "", param });
					i = end;
				}
				else
					tokens.push_back({ Kind::Text, std::string(1, body[i++]) });
			}
			return tokens;
		}

		// Named parameters before `...` take the leading arguments, those after it the trailing ones, and
		// `__VA_ARGS__` the arguments between, joined with ", ".
		std::string ArgumentFor(const MacroDefinition& def, const std::vector<std::string>& args, size_t param)
		{
			if (!def.IsVariadic()) return param < args.size() ? args[param] : "";
			const size_t ellipsis = def.params.size() - 1;
			if (param < ellipsis) return param < args.size() ? args[param] : "";
			std::string joined;
			for (size_t a = ellipsis; a < args.size(); ++a)
				joined += (joined.empty() ? "" : ", ") + args[a];
			return joined;
		}

		// `#x`: the argument as written, its spaces collapsed, quotes and backslashes in literals escaped.
		std::string Stringized(const std::string& argument)
		{
			std::string out = "\"";
			bool space = false;
			for (size_t i = 0; i < argument.size();)
			{
				if (StartsLiteral(argument, i))
				{
					const size_t end = LiteralEnd(argument, i);
					for (size_t k = i; k < end; ++k)
					{
						if (argument[k] == '"' || argument[k] == '\\') out += '\\';
						out += argument[k];
					}
					i = end;
					space = false;
					continue;
				}
				if (IsSpace(argument[i]))
				{
					if (!space) out += ' ';
					space = true;
					++i;
					continue;
				}
				out += argument[i++];
				space = false;
			}
			return out + "\"";
		}

		bool NextToPaste(const std::vector<BodyToken>& tokens, size_t i)
		{
			using Kind = BodyToken::Kind;
			for (size_t k = i; k-- > 0;)
				if (tokens[k].kind != Kind::Space) { if (tokens[k].kind == Kind::Paste) return true; break; }
			for (size_t k = i + 1; k < tokens.size(); ++k)
				if (tokens[k].kind != Kind::Space) return tokens[k].kind == Kind::Paste;
			return false;
		}

		void TrimTrailingSpace(std::string& out)
		{
			while (!out.empty() && IsSpace(out.back())) out.pop_back();
		}
	}

	std::string MacroExpander::Substitute(const MacroDefinition& def, const std::vector<std::string>& args)
	{
		using Kind = BodyToken::Kind;
		const std::vector<BodyToken> tokens = BodyTokens(def);
		std::string out;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			const BodyToken& token = tokens[i];
			switch (token.kind)
			{
			case Kind::Text:
			case Kind::Space:
				out += token.text;
				break;
			case Kind::Paste:
				TrimTrailingSpace(out);
				while (i + 1 < tokens.size() && tokens[i + 1].kind == Kind::Space) ++i;
				break;
			case Kind::Stringize:
				out += Stringized(ArgumentFor(def, args, token.param));
				break;
			case Kind::Param:
				out += NextToPaste(tokens, i) ? ArgumentFor(def, args, token.param)
					: ExpandedArgument(ArgumentFor(def, args, token.param));
				break;
			}
		}
		return out;
	}

	std::string MacroBodyError(const MacroDefinition& def)
	{
		using Kind = BodyToken::Kind;
		const std::vector<BodyToken> tokens = BodyTokens(def);
		size_t first = 0, last = tokens.size();
		while (first < last && tokens[first].kind == Kind::Space) ++first;
		while (last > first && tokens[last - 1].kind == Kind::Space) --last;
		if (first < last && (tokens[first].kind == Kind::Paste || tokens[last - 1].kind == Kind::Paste))
			return "'##' cannot start or end the body of the macro '" + def.name + "'";
		for (const BodyToken& token : tokens)
			if (def.isFunctionLike && token.kind == Kind::Text && token.text == "#")
				return "'#' in the macro '" + def.name + "' must be followed by one of its parameters";
		return "";
	}
}
