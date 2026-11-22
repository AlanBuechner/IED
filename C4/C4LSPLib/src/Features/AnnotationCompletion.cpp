#include "Features/AnnotationCompletion.h"
#include "Server/SemanticResolver.h"
#include "Model/DeclaredAnnotations.h"
#include "Parser/Nodes/Declarations/AnnotationDeclarationNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include <cctype>

namespace C4LSP::Features
{
	namespace
	{
		namespace Kind { constexpr int Function = 3; constexpr int Keyword = 14; }

		const char* const kModifiers[] = { "public", "private", "protected", "static", "virtual", "const", "inline", "abstract", "final",
			"volatile", "thread_local", "extern" };

		bool IsIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

		size_t SkipSpaceBack(const std::string& text, size_t at)
		{
			while (at > 0 && std::isspace(static_cast<unsigned char>(text[at - 1]))) --at;
			return at;
		}

		size_t SkipSpace(const std::string& text, size_t at)
		{
			while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
			return at;
		}

		// The start of the qualified name the caret ends (`Scene::Ra` of `[Scene::Ra|`).
		size_t NameStart(const std::string& text, size_t caret)
		{
			size_t at = caret;
			while (at > 0 && IsIdent(text[at - 1])) --at;
			while (at >= 2 && text[at - 1] == ':' && text[at - 2] == ':')
			{
				at -= 2;
				while (at > 0 && IsIdent(text[at - 1])) --at;
			}
			return at;
		}

		// A `[` that opens an annotation group: not an index (`a[`, `f()[`).
		bool OpensGroup(const std::string& text, size_t bracket)
		{
			const size_t before = SkipSpaceBack(text, bracket);
			return before == 0 || (!IsIdent(text[before - 1]) && text[before - 1] != ')');
		}

		// The `[` of the group a `,` at `comma` separates annotations in; npos when it is in parentheses or in no group.
		size_t GroupOf(const std::string& text, size_t comma)
		{
			int parens = 0;
			int brackets = 0;
			for (size_t i = comma; i-- > 0;)
			{
				const char c = text[i];
				if (c == ')') ++parens;
				else if (c == '(') { if (parens == 0) return std::string::npos; --parens; }
				else if (c == ']') ++brackets;
				else if (c == '[') { if (brackets == 0) return parens == 0 ? i : std::string::npos; --brackets; }
				else if ((c == ';' || c == '{' || c == '}') && parens == 0) return std::string::npos;
			}
			return std::string::npos;
		}

		// Inside a parameter list: an open `(` before the group's `[`, with no `;` or brace between.
		bool InParameters(const std::string& text, size_t bracket)
		{
			int parens = 0;
			for (size_t i = bracket; i-- > 0;)
			{
				const char c = text[i];
				if (c == ')') ++parens;
				else if (c == '(') { if (parens == 0) return true; --parens; }
				else if (c == ';' || c == '{' || c == '}') return false;
			}
			return false;
		}

		// Past the balanced `[...]` starting at `at`; npos when a `;` or brace comes first.
		size_t PastGroup(const std::string& text, size_t at)
		{
			int depth = 0;
			for (size_t i = at; i < text.size(); ++i)
			{
				const char c = text[i];
				if (c == '[' || c == '(') ++depth;
				else if (c == ']' || c == ')') { if (--depth == 0) return i + 1; }
				else if (c == ';' || c == '{' || c == '}') return std::string::npos;
			}
			return std::string::npos;
		}

		size_t WordStart(const std::string& text, size_t at)
		{
			while (at > 0 && IsIdent(text[at - 1])) --at;
			return at;
		}

		// Whether the word `word` ends just before `at`, starting a word of its own.
		bool WordEndsAt(const std::string& text, size_t at, const std::string& word)
		{
			return at >= word.size() && text.compare(at - word.size(), word.size(), word) == 0 && WordStart(text, at) == at - word.size();
		}

		// The `(` the `)` before `at` closes; npos when there is none.
		size_t OpeningParen(const std::string& text, size_t at)
		{
			int depth = 0;
			for (size_t i = at; i-- > 0;)
			{
				if (text[i] == ')') ++depth;
				else if (text[i] == '(' && --depth == 0) return i;
				else if (text[i] == ';' || text[i] == '{' || text[i] == '}') return std::string::npos;
			}
			return std::string::npos;
		}

		size_t WordEnd(const std::string& text, size_t at)
		{
			while (at < text.size() && IsIdent(text[at])) ++at;
			return at;
		}

		std::string WordAt(const std::string& text, size_t at)
		{
			size_t end = at;
			while (end < text.size() && IsIdent(text[end])) ++end;
			return text.substr(at, end - at);
		}

		bool IsModifier(const std::string& word)
		{
			for (const char* modifier : kModifiers)
				if (word == modifier) return true;
			return false;
		}

		// What the declaration after the annotation groups is, read from `at` (past the group the caret is in).
		std::optional<C4Compiler::AnnotationPlace> DeclarationAt(const std::string& text, size_t at, bool inType)
		{
			using C4Compiler::AnnotationPlace;
			at = SkipSpace(text, at);
			while (at < text.size() && text[at] == '[')
			{
				at = PastGroup(text, at);
				if (at == std::string::npos) return std::nullopt;
				at = SkipSpace(text, at);
			}
			std::string word = WordAt(text, at);
			while (IsModifier(word))
			{
				at = SkipSpace(text, at + word.size());
				word = WordAt(text, at);
			}
			if (word == "struct" || word == "class" || word == "interface" || word == "template") return AnnotationPlace::Type;
			if (word == "enum") return AnnotationPlace::Enum;
			if (word == "union") return AnnotationPlace::Union;
			if (word.empty()) return std::nullopt;
			for (size_t i = at; i < text.size(); ++i)
			{
				const char c = text[i];
				if (c == '(') return inType ? AnnotationPlace::Method : AnnotationPlace::Function;
				if (c == ';' || c == '=' || c == '{') return inType ? AnnotationPlace::Field : AnnotationPlace::Variable;
				if (c == '}' || c == '[') return std::nullopt;
			}
			return std::nullopt;
		}

		// Whether what follows the annotation groups from `at` is an access label (`private:`, not a member's `private int32 x;`).
		bool AccessLabelAt(const std::string& text, size_t at)
		{
			at = SkipSpace(text, at);
			while (at < text.size() && text[at] == '[')
			{
				at = PastGroup(text, at);
				if (at == std::string::npos) return false;
				at = SkipSpace(text, at);
			}
			const std::string word = WordAt(text, at);
			if (word != "public" && word != "private" && word != "protected") return false;
			const size_t colon = SkipSpace(text, at + word.size());
			return colon < text.size() && text[colon] == ':' && (colon + 1 >= text.size() || text[colon + 1] != ':');
		}

		std::string Detail(const C4Compiler::DeclaredAnnotations::Overloads& overloads)
		{
			std::string detail;
			for (const C4Compiler::AnnotationSignature* overload : overloads)
				detail += (detail.empty() ? "[" : " [") + std::string(overload->ToString().c_str()) + "]";
			return detail;
		}

		bool GoesThere(const C4Compiler::DeclaredAnnotations::Overloads& overloads, const AnnotationCompletion::Site& site)
		{
			using C4Compiler::AnnotationPlace;
			if (!site.onLabel && !site.place) return true;
			for (const C4Compiler::AnnotationSignature* overload : overloads)
			{
				if (site.onLabel && (overload->places.Has(AnnotationPlace::Field) || overload->places.Has(AnnotationPlace::Method))) return true;
				if (!site.onLabel && overload->places.Has(*site.place)) return true;
			}
			return false;
		}
	}

	std::optional<AnnotationCompletion::Site> AnnotationCompletion::Find(const std::string& text, size_t caret, bool inType)
	{
		caret = std::min(caret, text.size());
		const size_t before = SkipSpaceBack(text, NameStart(text, caret));
		if (before == 0) return std::nullopt;
		size_t bracket = std::string::npos;
		if (text[before - 1] == '[') bracket = before - 1;
		else if (text[before - 1] == ',') bracket = GroupOf(text, before - 1);
		if (bracket == std::string::npos || !OpensGroup(text, bracket)) return std::nullopt;

		Site site;
		const size_t nameStart = NameStart(text, caret);
		site.qualifier = text.substr(nameStart, WordStart(text, caret) - nameStart);
		if (InParameters(text, bracket)) site.place = C4Compiler::AnnotationPlace::Parameter;
		else
		{
			const size_t past = PastGroup(text, bracket);
			const size_t after = past != std::string::npos ? past : WordEnd(text, caret); // the group is still being typed: what follows the name
			site.onLabel = inType && AccessLabelAt(text, after);
			if (!site.onLabel) site.place = DeclarationAt(text, after, inType);
		}
		return site;
	}

	bool AnnotationCompletion::InPlaces(const std::string& text, size_t caret)
	{
		size_t at = SkipSpaceBack(text, WordStart(text, std::min(caret, text.size())));
		while (at > 0 && text[at - 1] == ',') // the places written before this one
		{
			const size_t place = SkipSpaceBack(text, at - 1);
			if (WordStart(text, place) == place) return false;
			at = SkipSpaceBack(text, WordStart(text, place));
		}
		const std::string on = C4Compiler::kAnnotationPlacesKeyword;
		if (!WordEndsAt(text, at, on)) return false;
		at = SkipSpaceBack(text, at - on.size());
		if (at > 0 && text[at - 1] == ')')
		{
			const size_t open = OpeningParen(text, at);
			if (open == std::string::npos) return false;
			at = SkipSpaceBack(text, open);
		}
		const size_t name = WordStart(text, at);
		if (name == at) return false;
		return WordEndsAt(text, SkipSpaceBack(text, name), C4Compiler::kAnnotationKeyword);
	}

	void AnnotationCompletion::AddPlaces(Core::Vector<CompletionItem>& out)
	{
		for (const C4Compiler::AnnotationPlace place : C4Compiler::AllAnnotationPlaces())
		{
			CompletionItem item;
			item.label = C4Compiler::AnnotationPlaceName(place);
			item.kind = Kind::Keyword;
			item.detail = std::string("the annotation goes on ") + C4Compiler::AnnotationPlaceNoun(place);
			item.sortText = std::to_string(static_cast<int>(place));
			out.Push(item);
		}
	}

	// An annotation is offered by its own name where, written after the site's qualifier, that name leads to it: the
	// annotations of the namespaces around the caret and the global one, in files this one includes.
	void AnnotationCompletion::Add(const Server::SemanticResolver& resolver, const Site& site, const C4Compiler::AnnotationContext& at,
		Core::Vector<CompletionItem>& out)
	{
		const C4Compiler::DeclaredAnnotations& declared = resolver.Annotations();
		for (const auto& [qualified, overloads] : declared.All())
		{
			const std::string name = overloads[0]->name.c_str();
			const C4Compiler::DeclaredAnnotations::Overloads named = declared.Find(site.qualifier + name, at);
			if (named.Empty() || named[0]->qualifiedName != overloads[0]->qualifiedName || !GoesThere(named, site)) continue;
			CompletionItem item;
			item.label = name;
			item.kind = Kind::Function;
			item.detail = Detail(named);
			item.sortText = "0" + name;
			out.Push(item);
		}
		if (!site.qualifier.empty() || site.onLabel) return; // the compiler's own are named alone, and go on one declaration
		for (const char* name : C4Compiler::CompilerAnnotationNames())
		{
			CompletionItem item;
			item.label = name;
			item.kind = Kind::Keyword;
			item.detail = "compiler annotation";
			item.sortText = std::string("1") + name;
			out.Push(item);
		}
	}
} // namespace C4LSP::Features
