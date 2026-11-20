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

		const Scripting::Type* Value(const Scripting::Type* type) { return Scripting::TypeHandle::UnwrapToValue(type); }

		const Scripting::ObjectType* ObjectOf(const Scripting::Type* type)
		{
			type = Value(type);
			return type && type->GetKind() == Scripting::TypeKind::Object ? static_cast<const Scripting::ObjectType*>(type) : nullptr;
		}

		// What a function or a callable value returns; a type that is neither is a method's, already its return type.
		const Scripting::Type* Returned(const Scripting::Type* type)
		{
			const Scripting::Type* value = Value(type);
			if (!value) return nullptr;
			switch (value->GetKind())
			{
			case Scripting::TypeKind::Function: return static_cast<const Scripting::FunctionType*>(value)->GetReturnType();
			case Scripting::TypeKind::FunPtr:
			case Scripting::TypeKind::Func: return static_cast<const Scripting::SignatureType*>(value)->GetReturnType();
			default: return type;
			}
		}

		// The return type of the operator `annotation` (`index`, `call`, `arrow`) of `type`'s object; null without one.
		const Scripting::Type* OperatorReturn(const Scripting::Type* type, const char* annotation)
		{
			for (const Scripting::ObjectType* object = ObjectOf(type); object; object = object->GetBaseTypes().Empty() ? nullptr : object->GetBaseTypes()[0])
				if (const Core::Vector<uint32>* methods = object->MethodsWithOperator(Core::String(annotation)))
					return object->GetMethods()[(*methods)[0]].m_ReturnType;
			return nullptr;
		}

		// What calling a value of `value`'s type returns: a callable's return type, an object's `()` operator's.
		const Scripting::Type* CallResult(const Scripting::Type* value)
		{
			switch (value->GetKind())
			{
			case Scripting::TypeKind::Function:
			case Scripting::TypeKind::FunPtr:
			case Scripting::TypeKind::Func: return Returned(value);
			case Scripting::TypeKind::Object: return OperatorReturn(value, "call");
			default: return nullptr;
			}
		}

		const Scripting::Type* Applied(const Scripting::Type* type, Suffix suffix)
		{
			const Scripting::Type* value = Value(type);
			if (!value) return nullptr;
			if (suffix == Suffix::Call) return CallResult(value);
			switch (value->GetKind())
			{
			case Scripting::TypeKind::Pointer: return static_cast<const Scripting::PointerType*>(value)->GetPointeeType();
			case Scripting::TypeKind::Array: return static_cast<const Scripting::ArrayType*>(value)->GetElementType();
			default: return OperatorReturn(value, "index");
			}
		}

		// What `->` reaches from `type`: a pointer's pointee, or what the object's `->` operator returns, dereferenced.
		const Scripting::Type* Arrowed(const Scripting::Type* type)
		{
			const Scripting::Type* value = Value(type);
			if (value && value->GetKind() == Scripting::TypeKind::Object)
				value = Value(OperatorReturn(value, "arrow"));
			return value && value->GetKind() == Scripting::TypeKind::Pointer ? static_cast<const Scripting::PointerType*>(value)->GetPointeeType() : value;
		}

		// A member of `type` by name: a field's type, or a method's return type (`method` set).
		const Scripting::Type* MemberType(const Scripting::Type* type, const std::string& name, bool& method)
		{
			method = false;
			const Scripting::Type* value = Value(type);
			if (!value) return nullptr;
			if (value->GetKind() == Scripting::TypeKind::Union)
			{
				for (const auto& field : static_cast<const Scripting::UnionType*>(value)->GetFields())
					if (field.mName == name) return field.m_Type;
				return nullptr;
			}
			const Scripting::ObjectType* object = ObjectOf(value);
			if (!object) return nullptr;
			if (const auto* field = object->GetFieldByName(Core::String(name))) return field->m_Type;
			if (const auto* found = object->FindMethodByName(Core::String(name)))
			{
				method = true;
				return found->m_ReturnType;
			}
			return nullptr;
		}

		bool DeclaresType(const Scripting::AstNode* declaration)
		{
			if (!declaration) return false;
			switch (declaration->Type())
			{
			case Scripting::NodeType::ObjectDefinition:
			case Scripting::NodeType::UnionDefinition:
			case Scripting::NodeType::EnumDefinition:
			case Scripting::NodeType::AliasDefinition: return true;
			default: return false;
			}
		}

		// The value a name has where the chain starts: its type, and whether its first call is spent on it (a function's
		// call, a type's construction).
		struct Start { const Scripting::Type* type = nullptr; bool called = false; };

		Start StartOfSymbol(const Scripting::SymbolInfo* info)
		{
			if (!info) return {};
			switch (info->kind)
			{
			case Scripting::SymbolKind::Function: return { info->functions.Empty() ? nullptr : Returned(info->functions[0]->type), true };
			case Scripting::SymbolKind::Type: return { info->type, true };
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

	const Scripting::Type* MemberChain::TypeIn(const Server::SemanticResolver& resolver, uint32 unitId, const Core::Vector<const Scripting::AstNode*>& path) const
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

		const Scripting::Type* type = start.type;
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
