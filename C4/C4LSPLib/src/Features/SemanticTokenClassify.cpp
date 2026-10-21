#include "Features/SemanticTokenWalk.h"
#include "Server/SemanticResolver.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Model/TypeHandle.h"    // TypeHandle::UnwrapToValue (alias/reference -> value type)
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/SwizzleViewNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace C4LSP::Features::TokenWalk
{
	uint32 ObjectKindToToken(Scripting::ObjectKind kind)
	{
		switch (kind)
		{
		case Scripting::ObjectKind::Struct:    return K(SemanticTokenKind::Struct);
		case Scripting::ObjectKind::Interface: return K(SemanticTokenKind::Interface);
		default:                               return K(SemanticTokenKind::Class);
		}
	}

	/// The object type a resolved symbol denotes (null when it is not an object type).
	const Scripting::ObjectType* AsObject(const Scripting::SymbolInfo* info)
	{
		if (!info || info->kind != Scripting::SymbolKind::Type || !info->type)
			return nullptr;
		return dynamic_cast<const Scripting::ObjectType*>(Scripting::TypeHandle::UnwrapToValue(info->type));
	}

	/// Classify a reference based on the declaration node it resolved to. A variable that is an
	/// object member (tracked in `fields`) counts as a field whether reached via `p.x` or a bare
	/// implicit-this identifier.
	uint32 DeclKind(const Scripting::AstNode* decl, bool isMemberAccess, const Ctx& ctx)
	{
		if (!decl)
			return isMemberAccess ? K(SemanticTokenKind::Field) : K(SemanticTokenKind::Variable);
		switch (decl->Type())
		{
		case Scripting::NodeType::FunctionDefinition:
			return isMemberAccess ? K(SemanticTokenKind::Method) : K(SemanticTokenKind::Function);
		case Scripting::NodeType::Parameter:
			return K(SemanticTokenKind::Variable); // house style: parameters are not a distinct color from variables
		case Scripting::NodeType::VariableDefinition:
			if (isMemberAccess || ctx.fields.count(decl))
				return K(SemanticTokenKind::Field);
			return K(SemanticTokenKind::Variable);
		case Scripting::NodeType::ObjectDefinition:
		case Scripting::NodeType::EnumDefinition:
		case Scripting::NodeType::UnionDefinition:
		case Scripting::NodeType::AliasDefinition:
			return K(SemanticTokenKind::Type);
		case Scripting::NodeType::Namespace:
			return K(SemanticTokenKind::Namespace);
		default:
			return isMemberAccess ? K(SemanticTokenKind::Field) : K(SemanticTokenKind::Variable);
		}
	}

	bool IsFunctionType(const Scripting::Type* type)
	{
		if (!type)
			return false;
		auto kind = type->GetKind();
		return kind == Scripting::TypeKind::Function || kind == Scripting::TypeKind::FunPtr;
	}

	/// Classify a reference (identifier / member access / static access) using the semantic
	/// result. `isMemberAccess` marks tokens reached through '.' or '::' on an object/type.
	uint32 ClassifyReference(const Ctx& ctx, const Scripting::AstNode& refNode, const Core::Vector<const Scripting::AstNode*>& path, bool isMemberAccess)
	{
		if (ctx.resolver)
		{
			const Server::NodeResolution res = ctx.resolver->ResolveReference(refNode, path);
			if (res.declaration)
				return DeclKind(res.declaration, isMemberAccess, ctx);

			if (res.type || res.isFunction)
			{
				// A method resolved without a declaring node carries its RETURN type: `isFunction` says what it is.
				if (isMemberAccess)
					return res.isFunction || IsFunctionType(res.type) ? K(SemanticTokenKind::Method) : K(SemanticTokenKind::Field);
				return res.isFunction ? K(SemanticTokenKind::Function) : K(SemanticTokenKind::Variable);
			}
		}
		return isMemberAccess ? K(SemanticTokenKind::Field) : K(SemanticTokenKind::Variable);
	}

	void RecordOffset(Ctx& ctx, const Scripting::SourceLocation& loc, TokInfo info)
	{
		if (loc.sourceId != ctx.sourceId || loc.line == 0 || loc.column == 0)
			return; // only this file's own tokens map to re-lexed editor positions

		// FIRST-WINS: a token can be reached by more than one AST node (a qualified chain segment is both the outer
		// StaticAccess/qualified-name owner AND an inner identifier visited during child recursion). The outer,
		// structurally-aware classification runs first and must not be clobbered by the inner node's plain
		// reference lookup - that was painting `Win` in `Win::Test::Func()` as a variable.
		if (ctx.byOffset.count({ loc.line, loc.column }))
			return;

		ctx.byOffset[{ loc.line, loc.column }] = info;
	}

	void RecordName(Ctx& ctx, const Scripting::SourceLocation& loc, const std::string& name, TokInfo info)
	{
		if (loc.sourceId != ctx.sourceId || name.empty())
			return;
		ctx.byName.emplace(name, info); // first declaration wins; references never overwrite
	}

	/// Populate `ctx.namespacePaths` from the compiled program's scope tree via the resolver.
	void BuildNamespacePaths(Ctx& ctx)
	{
		if (ctx.resolver)
			ctx.resolver->CollectNamespacePaths(ctx.namespacePaths);
	}

	/// True when `name` is itself a namespace path, or starts with one followed by "::" (a longer
	/// qualified chain rooted in that namespace). Used to color the namespace segments of chains.
	bool IsNamespacePrefix(const Ctx& ctx, const std::string& name)
	{
		if (ctx.namespacePaths.count(name))
			return true;
		size_t p = 0;
		while ((p = name.find("::", p)) != std::string::npos)
		{
			p += 2; // the prefix candidate ends at this separator
			if (ctx.namespacePaths.count(name.substr(0, p - 2)))
				return true;
		}
		return false;
	}

	/// Classify ONE segment of a qualified reference chain (`A::B::C`) by resolving the prefix
	/// `seg0::...::thisSegment`:
	///   - namespace scope path            -> Namespace kind (the chain continues through it)
	///   - type declaration                -> that type's own kind (class/struct/enum gold...)
	///   - final segment resolving to a member of an object/union type via the model or direct
	///     member lookup (incl. inherited + extension members and scoped enum constants)
	///                                     -> Method / Field kind
	/// Unresolvable segments fall back conservatively: non-final segments read as namespaces,
	/// the final unresolvable segment keeps the legacy field fallback so coloring never regresses
	/// for symbols analysis missed.
	uint32 ClassifyChainSegment(const Ctx& ctx, const std::vector<std::string>& segments, size_t i)
	{
		const bool isLast = (i + 1 == segments.size());

		std::string prefix; // outermost..this segment
		for (size_t j = 0; j <= i; ++j)
			prefix += (j ? "::" : "") + segments[j];

		// Resolve the prefix against the model ONCE: a resolved TYPE beats namespace-prefix
		// matching for non-final segments, because C4 has no nested namespaces - `Win::Test` inside
		// `namespace Win { class Test ... }` is the type path to Test, not a namespace named "Win::Test".
		const Scripting::SymbolInfo* entry = nullptr;
		if (ctx.resolver)
			entry = ctx.resolver->Lookup(prefix, 0);

		if (!isLast)
		{
			// `template<T> class X` - the T in an argument position is a type parameter use, not a type.
			if (ctx.templateParamNames.count(segments[i]))
				return K(SemanticTokenKind::TypeParameter);

			// A declared TYPE used as a chain owner (`Test` in `Test::DoThing()`): its simple name may not resolve
			// through the qualified lookup, so consult the walk's type-name table and color it with its own declaration
			// kind instead of dropping to the conservative namespace fallback. Only when the qualified lookup found
			// NOTHING for this prefix - a real resolution above wins over a simple-name collision.
			if (!entry)
			{
				auto tnIt = ctx.typeNames.find(segments[i]);
				if (tnIt != ctx.typeNames.end())
					return tnIt->second;
			}

			if (entry && entry->kind == Scripting::SymbolKind::Type)
			{
				// A resolved owner type: color it as its own declaration kind.
				if (const Scripting::AstNode* decl = entry->declNode)
				{
					switch (decl->Type())
					{
					case Scripting::NodeType::ObjectDefinition:
						return ObjectKindToToken(static_cast<const Scripting::ObjectDefinitionNode*>(decl)->Kind());
					case Scripting::NodeType::EnumDefinition:
						return K(SemanticTokenKind::Enum);
					default:
						break; // alias/other type decls keep the generic type color below
					}
				}
				return K(SemanticTokenKind::Type); // `SomeClass::` prefix of a member chain
			}

			// A namespace the chain continues through. Checked AFTER resolved-type matching (above) so a
			// qualified owner type keeps its own kind; still correct for plain namespace chains because a
			// bare namespace name is never registered as a Type symbol under that exact prefix.
			if (ctx.namespacePaths.count(prefix))
				return K(SemanticTokenKind::Namespace);
		}

		// A prefix that is itself a type name: color it as its own declaration kind.
		if (entry && entry->kind == Scripting::SymbolKind::Type)
		{
			if (const Scripting::AstNode* decl = entry->declNode)
			{
				switch (decl->Type())
				{
				case Scripting::NodeType::ObjectDefinition:
					return ObjectKindToToken(static_cast<const Scripting::ObjectDefinitionNode*>(decl)->Kind());
				case Scripting::NodeType::EnumDefinition:
					return K(SemanticTokenKind::Enum);
				default:
					break; // alias/other type decls keep the generic type color below
				}
			}
			if (!isLast)
				return K(SemanticTokenKind::Type); // `SomeClass::` prefix of a member chain

			// FINAL segment is a TYPE name: if it resolved through a namespace, that's its
			// definition; otherwise (unqualified local type in this file) same kind applies.
			if (prefix.find("::") != std::string::npos || ctx.namespacePaths.count(prefix))
				return K(SemanticTokenKind::Type); // qualified type reference: generic gold
		}

		const bool isMemberAccess = prefix.find("::") != std::string::npos;
		if (isLast && isMemberAccess)
		{
			// `Owner::member` - resolve the member against a resolved owner type.
			size_t colonPos = prefix.rfind("::");
			std::string ownerPrefix = prefix.substr(0, colonPos); // may be empty for local access
			const char* memberName = segments[segments.size() - 1].c_str();

			// (a) Symbol route: a static/global symbol registered under the qualified name.
			if (!entry && ctx.resolver)
				entry = ctx.resolver->Lookup(prefix, 0);
			if (entry)
			{
				switch (entry->kind)
				{
				case Scripting::SymbolKind::Function:
					return K(SemanticTokenKind::Method); // static/extension or free member function
				case Scripting::SymbolKind::Variable:
					return K(SemanticTokenKind::Field);  // a (static) member variable
				default:
					break;
				}
			}

			// (b) Direct member route: owner type's fields/methods incl. inheritance + extensions,
			// and scoped-enum/union members the symbol table doesn't register as qualified names.
			const Scripting::SymbolInfo* owner = nullptr;
			if (!ownerPrefix.empty() && ctx.resolver)
				owner = ctx.resolver->Lookup(ownerPrefix, 0);

			if (const Scripting::ObjectType* objType = AsObject(owner))
			{
				auto hasMethod = [&](const Scripting::ObjectType* t) {
					for (const auto& m : t->GetMethods())
						if (m.mName == memberName) return true;
					for (const auto& m : t->GetExtensionMethods())
						if (m.mName == memberName) return true;
					return false;
				};
				auto hasField = [&](const Scripting::ObjectType* t) {
					for (const auto& f : t->GetFields())
						if (f.mName == memberName) return true;
					return false;
				};
				std::function<bool(const Scripting::ObjectType*)> walkInherited = [&](const Scripting::ObjectType* t) -> bool {
					for (const auto* base : t->GetBaseTypes())
						if (base && walkInherited(base)) return true;
					return hasMethod(t) || hasField(t);
				};
				if (walkInherited(objType))
					return K(SemanticTokenKind::Method); // member function reference through the chain

				std::function<bool(const Scripting::ObjectType*)> walkFields = [&](const Scripting::ObjectType* t) -> bool {
					for (const auto* base : t->GetBaseTypes())
						if (base && walkFields(base)) return true;
					return hasField(t);
				};
				if (walkFields(objType))
					return K(SemanticTokenKind::Field); // static member variable reference

				if (const Scripting::EnumType* enumType = owner ? dynamic_cast<const Scripting::EnumType*>(Scripting::TypeHandle::UnwrapToValue(owner->type)) : nullptr)
				{
					for (const auto& m : enumType->GetMembers())
						if (m.mName == memberName) return K(SemanticTokenKind::Variable); // scoped constant
				}
				if (const Scripting::UnionType* unionType = owner ? dynamic_cast<const Scripting::UnionType*>(Scripting::TypeHandle::UnwrapToValue(owner->type)) : nullptr)
				{
					for (const auto& f : unionType->GetFields())
						if (f.mName == memberName) return K(SemanticTokenKind::Field);
				}
			}
		}

		return isLast ? K(SemanticTokenKind::Field) : K(SemanticTokenKind::Namespace); // conservative fallbacks
	}

	/// Classify a type reference (a QualType or one of its template-argument types): primitives read in the
	/// keyword family + builtin, user-defined types with their OWN declaration kind, `T` as typeParameter. The
	/// resolved type comes from the model's own type verb (the compiler is the source of truth); a single-segment
	/// name that resolves to NOTHING is not a primitive. First-wins in RecordOffset keeps this from clobbering a
	/// classification another node already recorded for the same offset.
	void ClassifyTypeSegments(const Scripting::QualTypeNode& qt, Ctx& ctx, const Core::Vector<const Scripting::AstNode*>& path)
	{
		if (!qt.BaseName())
			return;

		bool isPrimitive = false; // resolved through the model's own type verb
		if (ctx.resolver)
		{
			const Scripting::Type* resolved = ctx.resolver->ResolveQualType(qt, path);
			isPrimitive = resolved && Scripting::TypeHandle::UnwrapToValue(resolved)->IsPrimitive();
		}

		std::string prefix; // outermost..this segment, for namespace / type resolution
		for (const auto& seg : qt.BaseName()->GetSegments())
		{
			if (seg.nameToken.GetType() != Scripting::TokenType::Identifier)
				continue;
			prefix += (prefix.empty() ? "" : "::") + std::string(seg.nameToken.GetText());

			uint32 kind = K(SemanticTokenKind::Type); // user-defined type reference: gold, in any context
			uint32 mods = None;
			if (isPrimitive)
				mods |= SemanticTokenModifiers::Builtin;
			else if (ctx.templateParamNames.count(seg.nameToken.GetText()))
				kind = K(SemanticTokenKind::TypeParameter); // `Box<T>`: a declared parameter use, not a type
			else if (ctx.resolver)
			{
				// A user-defined type in ANY context (member variable, parameter, ...): color it with its own
				// declaration kind instead of the generic gold. The class/enum family is distinct from `type`.
				const Scripting::SymbolInfo* entry = ctx.resolver->Lookup(prefix, 0);
				if (entry && entry->kind == Scripting::SymbolKind::Type)
				{
					if (const Scripting::AstNode* decl = entry->declNode)
					{
						if (decl->Type() == Scripting::NodeType::ObjectDefinition)
							kind = ObjectKindToToken(static_cast<const Scripting::ObjectDefinitionNode*>(decl)->Kind());
						else if (decl->Type() == Scripting::NodeType::EnumDefinition)
							kind = K(SemanticTokenKind::Enum);
					}
				}
				else if (!entry && !IsNamespacePrefix(ctx, prefix))
				{
					// Nested types (`struct Vec2` inside `class Test`) don't resolve through the global
					// qualified lookup; fall back to the walk's type-name table so they still read with
					// their own declaration kind (struct) rather than generic gold.
					auto tnIt = ctx.typeNames.find(seg.nameToken.GetText());
					if (tnIt != ctx.typeNames.end())
						kind = tnIt->second;
				}
			}

			if (kind == K(SemanticTokenKind::Type) && !mods && IsNamespacePrefix(ctx, prefix))
				// Namespace coloring is the CONSERVATIVE FALLBACK only: a segment that resolved to a real type
				// above keeps its declaration kind. Without this ordering `Engine::Thing` (a struct living in
				// namespace Engine) would read as a NAMESPACE because "Engine" is a valid namespace prefix -
				// the type must win, exactly like VS2022/Visual Assist color qualified names per segment.
				kind = K(SemanticTokenKind::Namespace); // genuine namespace chain: Win::Sub in `Win::Sub::Thing`

			RecordOffset(ctx, seg.nameToken.GetStartLocation(), TokInfo{ kind, mods });

			// Template arguments (`Box<int32>`) hang off the segment as a side field - NOT children of this
			// node or its base name - so no other walk case ever reaches them. Classify each argument with its
			// OWN declaration kind (primitives keyword+builtin, user types gold family, `T` typeParameter),
			// exactly like the surrounding type context; first-wins keeps this from clobbering anything else.
			if (seg.templateArgs)
				WalkTemplateArgument(*seg.templateArgs, ctx, path);
		}
	}
} // namespace C4LSP::Features::TokenWalk
