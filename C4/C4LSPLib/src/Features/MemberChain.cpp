#include "Features/MemberChain.h"
#include "Server/SemanticResolver.h"
#include "Model/TypeHandle.h"
#include "Model/Types.h"
#include "Parser/AstNode.h"
#include <cctype>

namespace C4LSP::Features
{
	namespace
	{
		using Access = MemberChain::Access;
		using Suffix = MemberChain::Suffix;
		using Link = MemberChain::Link;
		constexpr size_t npos = std::string::npos;

		bool IsIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

		size_t SkipSpaceBack(const std::string& text, size_t at)
		{
			while (at > 0 && std::isspace(static_cast<unsigned char>(text[at - 1]))) --at;
			return at;
		}

		// Back over spaces and tabs on the same line: a name beginning a line is no member of an accessor ending the line
		// before (`d.` left dangling above `v.`), while an accessor beginning a line continues the line before (`a\n\t.b`).
		size_t SkipBlanksBack(const std::string& text, size_t at)
		{
			while (at > 0 && (text[at - 1] == ' ' || text[at - 1] == '\t' || text[at - 1] == '\r')) --at;
			return at;
		}

		struct Accessor { Access access; size_t start; };

		// The accessor whose last character is just before `at`; none when the text there is not `.`, `->` or `::`.
		std::optional<Accessor> AccessorEndingAt(const std::string& text, size_t at)
		{
			if (at >= 2 && text[at - 1] == '>' && text[at - 2] == '-') return Accessor{ Access::Arrow, at - 2 };
			if (at >= 2 && text[at - 1] == ':' && text[at - 2] == ':') return Accessor{ Access::Scope, at - 2 };
			if (at >= 1 && text[at - 1] == '.' && !(at >= 2 && text[at - 2] == '.')) return Accessor{ Access::Dot, at - 1 };
			return std::nullopt;
		}

		// Where the quoted literal whose closing quote is at `close` opens; npos when it does not on its line.
		size_t LiteralStart(const std::string& text, size_t close)
		{
			const char quote = text[close];
			for (size_t i = close; i-- > 0;)
			{
				if (text[i] == '\n') return npos;
				if (text[i] == quote && (i == 0 || text[i - 1] != '\\')) return i;
			}
			return npos;
		}

		// Where the bracket closed just before `at` opens (`(`, `[` or `<`); npos when no statement before it opens it.
		size_t OpeningOf(const std::string& text, size_t at)
		{
			const char close = text[at - 1];
			const char open = close == ')' ? '(' : close == ']' ? '[' : '<';
			int depth = 0;
			for (size_t i = at; i-- > 0;)
			{
				const char c = text[i];
				if (c == '"' || c == '\'')
				{
					i = LiteralStart(text, i);
					if (i == npos) return npos;
				}
				else if (c == close) ++depth;
				else if (c == open && --depth == 0) return i;
				else if (c == ';' || c == '{' || c == '}') return npos;
			}
			return npos;
		}

		// The link ending just before `at`, read backwards: its calls and indexes, template arguments, then its name. Sets
		// `start` to where its name begins; nullopt when no name is there.
		std::optional<Link> LinkEndingAt(const std::string& text, size_t at, size_t& start)
		{
			Link link;
			at = SkipSpaceBack(text, at);
			while (at > 0 && (text[at - 1] == ')' || text[at - 1] == ']'))
			{
				const Suffix suffix = text[at - 1] == ')' ? Suffix::Call : Suffix::Index;
				const size_t open = OpeningOf(text, at);
				if (open == npos) return std::nullopt;
				link.suffixes.insert(link.suffixes.begin(), suffix);
				at = SkipSpaceBack(text, open);
			}
			if (at > 0 && text[at - 1] == '>' && !(at >= 2 && text[at - 2] == '-')) // `Make<Mesh>()`: the arguments name no member
			{
				const size_t open = OpeningOf(text, at);
				if (open == npos) return std::nullopt;
				at = SkipSpaceBack(text, open);
			}
			start = at;
			while (start > 0 && IsIdentChar(text[start - 1])) --start;
			if (start == at || std::isdigit(static_cast<unsigned char>(text[start]))) return std::nullopt;
			link.name = text.substr(start, at - start);
			return link;
		}

		const C4Compiler::Type* Value(const C4Compiler::Type* type) { return C4Compiler::TypeHandle::UnwrapToValue(type); }

		const C4Compiler::ObjectType* ObjectOf(const C4Compiler::Type* type)
		{
			type = Value(type);
			return type && type->GetKind() == C4Compiler::TypeKind::Object ? static_cast<const C4Compiler::ObjectType*>(type) : nullptr;
		}

		// What a function or a callable value returns; a type that is neither is a method's, already its return type.
		const C4Compiler::Type* Returned(const C4Compiler::Type* type)
		{
			const C4Compiler::Type* value = Value(type);
			if (!value) return nullptr;
			switch (value->GetKind())
			{
			case C4Compiler::TypeKind::Function: return static_cast<const C4Compiler::FunctionType*>(value)->GetReturnType();
			case C4Compiler::TypeKind::FunPtr:
			case C4Compiler::TypeKind::Func: return static_cast<const C4Compiler::SignatureType*>(value)->GetReturnType();
			default: return type;
			}
		}

		// The return type of the operator `annotation` (`index`, `call`, `arrow`) of `type`'s object; null without one.
		const C4Compiler::Type* OperatorReturn(const C4Compiler::Type* type, const char* annotation)
		{
			for (const C4Compiler::ObjectType* object = ObjectOf(type); object; object = object->GetBaseTypes().Empty() ? nullptr : object->GetBaseTypes()[0])
				if (const Core::Vector<uint32>* methods = object->MethodsWithOperator(Core::String(annotation)))
					return object->GetMethods()[(*methods)[0]].m_ReturnType;
			return nullptr;
		}

		// What calling a value of `value`'s type returns: a callable's return type, an object's `()` operator's.
		const C4Compiler::Type* CallResult(const C4Compiler::Type* value)
		{
			switch (value->GetKind())
			{
			case C4Compiler::TypeKind::Function:
			case C4Compiler::TypeKind::FunPtr:
			case C4Compiler::TypeKind::Func: return Returned(value);
			case C4Compiler::TypeKind::Object: return OperatorReturn(value, "call");
			default: return nullptr;
			}
		}

		const C4Compiler::Type* Applied(const C4Compiler::Type* type, Suffix suffix)
		{
			const C4Compiler::Type* value = Value(type);
			if (!value) return nullptr;
			if (suffix == Suffix::Call) return CallResult(value);
			switch (value->GetKind())
			{
			case C4Compiler::TypeKind::Pointer: return static_cast<const C4Compiler::PointerType*>(value)->GetPointeeType();
			case C4Compiler::TypeKind::Array: return static_cast<const C4Compiler::ArrayType*>(value)->GetElementType();
			default: return OperatorReturn(value, "index");
			}
		}

		// What `->` reaches from `type`: a pointer's pointee, or what the object's `->` operator returns, dereferenced.
		const C4Compiler::Type* Arrowed(const C4Compiler::Type* type)
		{
			const C4Compiler::Type* value = Value(type);
			if (value && value->GetKind() == C4Compiler::TypeKind::Object)
				value = Value(OperatorReturn(value, "arrow"));
			return value && value->GetKind() == C4Compiler::TypeKind::Pointer ? static_cast<const C4Compiler::PointerType*>(value)->GetPointeeType() : value;
		}

		// A member of `type` by name: a field's type, or a method's return type (`method` set).
		const C4Compiler::Type* MemberType(const C4Compiler::Type* type, const std::string& name, bool& method)
		{
			method = false;
			const C4Compiler::Type* value = Value(type);
			if (!value) return nullptr;
			if (value->GetKind() == C4Compiler::TypeKind::Union)
			{
				for (const auto& field : static_cast<const C4Compiler::UnionType*>(value)->GetFields())
					if (field.mName == name) return field.m_Type;
				return nullptr;
			}
			const C4Compiler::ObjectType* object = ObjectOf(value);
			if (!object) return nullptr;
			if (const auto* field = object->GetFieldByName(Core::String(name))) return field->m_Type;
			if (const auto* found = object->FindMethodByName(Core::String(name)))
			{
				method = true;
				return found->m_ReturnType;
			}
			return nullptr;
		}

		bool DeclaresType(const C4Compiler::AstNode* declaration)
		{
			if (!declaration) return false;
			switch (declaration->Type())
			{
			case C4Compiler::NodeType::ObjectDefinition:
			case C4Compiler::NodeType::UnionDefinition:
			case C4Compiler::NodeType::EnumDefinition:
			case C4Compiler::NodeType::AliasDefinition: return true;
			default: return false;
			}
		}

		// The value a name has where the chain starts: its type, and whether its first call is spent on it (a function's
		// call, a type's construction).
		struct Start { const C4Compiler::Type* type = nullptr; bool called = false; };

		Start StartOfSymbol(const C4Compiler::SymbolInfo* info)
		{
			if (!info) return {};
			switch (info->kind)
			{
			case C4Compiler::SymbolKind::Function: return { info->functions.Empty() ? nullptr : Returned(info->functions[0]->type), true };
			case C4Compiler::SymbolKind::Type: return { info->type, true };
			default: return { info->type, false };
			}
		}
	}

	std::optional<MemberChain> MemberChain::Before(const std::string& text, size_t offset)
	{
		size_t at = std::min(offset, text.size());
		while (at > 0 && IsIdentChar(text[at - 1])) --at; // the member being typed
		std::optional<Accessor> accessor = AccessorEndingAt(text, SkipBlanksBack(text, at));
		if (!accessor || accessor->access == Access::Scope) return std::nullopt; // `A::` qualifies a name: no value is accessed
		std::vector<Link> links;
		while (accessor)
		{
			size_t start = 0;
			std::optional<Link> link = LinkEndingAt(text, accessor->start, start);
			if (!link) return std::nullopt;
			link->next = accessor->access;
			links.insert(links.begin(), std::move(*link));
			accessor = AccessorEndingAt(text, SkipBlanksBack(text, start));
		}
		return MemberChain(std::move(links));
	}

	const C4Compiler::Type* MemberChain::TypeIn(const Server::SemanticResolver& resolver, uint32 unitId, const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		// The leading links joined by `::` are one name (`Math::FVector3::Up`).
		size_t at = 0;
		std::string name = m_Links[0].name;
		while (at + 1 < m_Links.size() && m_Links[at].next == Access::Scope && m_Links[at].suffixes.empty())
			name += "::" + m_Links[++at].name;

		Start start;
		if (at > 0) start = StartOfSymbol(resolver.Lookup(name, unitId, &path));
		else if (name == "this") start = { resolver.ImplicitThis(path), false };
		else
		{
			const Server::NodeResolution named = resolver.ResolveIdentifier(name, path);
			start = { named.isFunction ? Returned(named.type) : named.type, named.isFunction || DeclaresType(named.declaration) };
		}

		const C4Compiler::Type* type = start.type;
		bool called = start.called;
		for (;;)
		{
			for (Suffix suffix : m_Links[at].suffixes)
			{
				if (suffix == Suffix::Call && called) called = false; // the call the name itself is
				else type = Applied(type, suffix);
				if (!type) return nullptr;
			}
			if (m_Links[at].next == Access::Arrow) type = Arrowed(type);
			if (!type || ++at == m_Links.size()) return type;
			type = MemberType(type, m_Links[at].name, called);
			if (!type) return nullptr;
		}
	}
} // namespace C4LSP::Features
