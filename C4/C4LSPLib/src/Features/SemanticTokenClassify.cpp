#include "Features/SemanticTokenWalk.h"
#include "Features/TypeText.h"
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
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace C4LSP::Features::TokenWalk
{
	uint32 ObjectKindToToken(C4Compiler::ObjectKind kind)
	{
		switch (kind)
		{
		case C4Compiler::ObjectKind::Struct:    return K(SemanticTokenKind::Struct);
		case C4Compiler::ObjectKind::Interface: return K(SemanticTokenKind::Interface);
		default:                               return K(SemanticTokenKind::Class);
		}
	}

	/// The object type a resolved symbol denotes (null when it is not an object type).
	const C4Compiler::ObjectType* AsObject(const C4Compiler::SymbolInfo* info)
	{
		if (!info || info->kind != C4Compiler::SymbolKind::Type || !info->type)
			return nullptr;
		return dynamic_cast<const C4Compiler::ObjectType*>(C4Compiler::TypeHandle::UnwrapToValue(info->type));
	}

	uint32 TypeDeclKind(const C4Compiler::AstNode* decl)
	{
		if (decl && decl->Type() == C4Compiler::NodeType::ObjectDefinition)
			return ObjectKindToToken(static_cast<const C4Compiler::ObjectDefinitionNode*>(decl)->Kind());
		if (decl && decl->Type() == C4Compiler::NodeType::EnumDefinition)
			return K(SemanticTokenKind::Enum);
		return K(SemanticTokenKind::Type);
	}

	bool HasMethod(const C4Compiler::ObjectType* object, const std::string& name)
	{
		if (!object)
			return false;
		for (const auto& m : object->GetMethods())
			if (m.mName == name) return true;
		for (const auto& m : object->GetExtensionMethods())
			if (m.mName == name) return true;
		for (const auto* base : object->GetBaseTypes())
			if (HasMethod(base, name)) return true;
		return false;
	}

	bool HasField(const C4Compiler::ObjectType* object, const std::string& name)
	{
		if (!object)
			return false;
		for (const auto& f : object->GetFields())
			if (f.mName == name) return true;
		for (const auto* base : object->GetBaseTypes())
			if (HasField(base, name)) return true;
		return false;
	}

	bool OwnerMemberKind(const C4Compiler::SymbolInfo* owner, const std::string& name, uint32& kind)
	{
		if (!owner)
			return false;
		if (const C4Compiler::ObjectType* object = AsObject(owner))
		{
			if (HasMethod(object, name))
			{
				kind = K(SemanticTokenKind::Method);
				return true;
			}
			kind = K(SemanticTokenKind::Field);
			return HasField(object, name);
		}
		const C4Compiler::Type* value = C4Compiler::TypeHandle::UnwrapToValue(owner->type);
		if (const auto* enumType = dynamic_cast<const C4Compiler::EnumType*>(value))
		{
			kind = K(SemanticTokenKind::Variable); // a scoped constant
			for (const auto& m : enumType->GetMembers())
				if (m.mName == name) return true;
		}
		if (const auto* unionType = dynamic_cast<const C4Compiler::UnionType*>(value))
		{
			kind = K(SemanticTokenKind::Field);
			for (const auto& f : unionType->GetFields())
				if (f.mName == name) return true;
		}
		return false;
	}

	/// A bare name inside a member function that names a method of its object: an implicit-this call.
	bool IsImplicitThisMethod(const Ctx& ctx, const C4Compiler::AstNode& refNode, const Core::Vector<const C4Compiler::AstNode*>& path)
	{
		if (!ctx.resolver || refNode.Type() != C4Compiler::NodeType::IdentifierExpression)
			return false;
		const auto& id = static_cast<const C4Compiler::IdentifierExpressionNode&>(refNode);
		return HasMethod(ctx.resolver->EnclosingObject(path), std::string(id.GetNameToken().GetText()));
	}

	/// Classify a reference based on the declaration node it resolved to. A variable that is an
	/// object member (tracked in `fields`) counts as a field whether reached via `p.x` or a bare
	/// implicit-this identifier.
	uint32 DeclKind(const C4Compiler::AstNode* decl, bool isMemberAccess, const Ctx& ctx)
	{
		if (!decl)
			return isMemberAccess ? K(SemanticTokenKind::Field) : K(SemanticTokenKind::Variable);
		switch (decl->Type())
		{
		case C4Compiler::NodeType::FunctionDefinition:
			return isMemberAccess ? K(SemanticTokenKind::Method) : K(SemanticTokenKind::Function);
		case C4Compiler::NodeType::Parameter:
			return K(SemanticTokenKind::Variable); // house style: parameters are not a distinct color from variables
		case C4Compiler::NodeType::VariableDefinition:
			if (isMemberAccess || ctx.fields.count(decl))
				return K(SemanticTokenKind::Field);
			return K(SemanticTokenKind::Variable);
		case C4Compiler::NodeType::ObjectDefinition:
		case C4Compiler::NodeType::EnumDefinition:
		case C4Compiler::NodeType::UnionDefinition:
		case C4Compiler::NodeType::AliasDefinition:
			return TypeDeclKind(decl);
		case C4Compiler::NodeType::Namespace:
			return K(SemanticTokenKind::Namespace);
		default:
			return isMemberAccess ? K(SemanticTokenKind::Field) : K(SemanticTokenKind::Variable);
		}
	}

	bool IsFunctionType(const C4Compiler::Type* type)
	{
		if (!type)
			return false;
		auto kind = type->GetKind();
		return kind == C4Compiler::TypeKind::Function || kind == C4Compiler::TypeKind::FunPtr;
	}

	/// Classify a reference (identifier / member access / static access) using the semantic
	/// result. `isMemberAccess` marks tokens reached through '.' or '::' on an object/type.
	uint32 ClassifyReference(const Ctx& ctx, const C4Compiler::AstNode& refNode, const Core::Vector<const C4Compiler::AstNode*>& path, bool isMemberAccess)
	{
		if (ctx.resolver)
		{
			const Server::NodeResolution res = ctx.resolver->ResolveReference(refNode, path);
			const bool function = res.isFunction || (res.declaration && res.declaration->Type() == C4Compiler::NodeType::FunctionDefinition);
			if (!isMemberAccess && function && IsImplicitThisMethod(ctx, refNode, path))
				return K(SemanticTokenKind::Method);
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

	void RecordOffset(Ctx& ctx, const C4Compiler::SourceLocation& loc, TokInfo info)
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

	void RecordName(Ctx& ctx, const C4Compiler::SourceLocation& loc, const std::string& name, TokInfo info)
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
		const C4Compiler::SymbolInfo* entry = nullptr;
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

			if (entry && entry->kind == C4Compiler::SymbolKind::Type)
				return TypeDeclKind(entry->declNode); // a resolved owner type: `SomeClass::` of a member chain

			// A namespace the chain continues through. Checked AFTER resolved-type matching (above) so a
			// qualified owner type keeps its own kind; still correct for plain namespace chains because a
			// bare namespace name is never registered as a Type symbol under that exact prefix.
			if (ctx.namespacePaths.count(prefix))
				return K(SemanticTokenKind::Namespace);
		}

		// The FINAL segment is a type name: its own declaration kind; an alias reached through a namespace is
		// generic gold.
		if (entry && entry->kind == C4Compiler::SymbolKind::Type)
		{
			const uint32 kind = TypeDeclKind(entry->declNode);
			if (kind != K(SemanticTokenKind::Type) || prefix.find("::") != std::string::npos || ctx.namespacePaths.count(prefix))
				return kind;
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
				case C4Compiler::SymbolKind::Function:
					return K(SemanticTokenKind::Method); // static/extension or free member function
				case C4Compiler::SymbolKind::Variable:
					return K(SemanticTokenKind::Field);  // a (static) member variable
				default:
					break;
				}
			}

			// (b) Direct member route: owner type's fields/methods incl. inheritance + extensions,
			// and scoped-enum/union members the symbol table doesn't register as qualified names.
			const C4Compiler::SymbolInfo* owner = nullptr;
			if (!ownerPrefix.empty() && ctx.resolver)
				owner = ctx.resolver->Lookup(ownerPrefix, 0);
			uint32 memberKind = 0;
			if (OwnerMemberKind(owner, memberName, memberKind))
				return memberKind;
		}

		return isLast ? K(SemanticTokenKind::Field) : K(SemanticTokenKind::Namespace); // conservative fallbacks
	}

	/// One segment of a type reference, `prefix` naming it from the outermost segment, looked up from where `path`
	/// declares it. A type the scopes declare keeps its own declaration kind - an alias its generic `type`, never taken
	/// for an unresolved name (BUG-471) - and the namespace color is the fallback for a name that declares no type, so
	/// `Engine::Thing` reads as a struct although "Engine" is a namespace prefix. `endsPrimitive`: the segment is the
	/// last of a type that resolves to a primitive; it is builtin only when it names the primitive itself (`int32`),
	/// not an alias of one.
	static TokInfo ClassifyTypeSegment(const Ctx& ctx, const std::string& prefix, const std::string& name, bool endsPrimitive,
		const Core::Vector<const C4Compiler::AstNode*>& path)
	{
		const C4Compiler::SymbolInfo* entry = ctx.resolver ? ctx.resolver->Lookup(prefix, 0, &path) : nullptr;
		const bool declaresType = entry && entry->kind == C4Compiler::SymbolKind::Type;
		if (endsPrimitive && !declaresType)
			return { K(SemanticTokenKind::Type), SemanticTokenModifiers::Builtin };
		if (ctx.templateParamNames.count(name))
			return { K(SemanticTokenKind::TypeParameter), None }; // `Box<T>`: a declared parameter use, not a type
		if (declaresType)
			return { TypeDeclKind(entry->declNode), None };
		if (IsNamespacePrefix(ctx, prefix))
			return { K(SemanticTokenKind::Namespace), None };
		// A nested type (`struct Vec2` inside `class Test`) the lookup misses: the walk's type-name table has its kind.
		auto declared = entry ? ctx.typeNames.end() : ctx.typeNames.find(name);
		return { declared != ctx.typeNames.end() ? declared->second : K(SemanticTokenKind::Type), None };
	}

	/// Classify a type reference (a QualType or one of its template-argument types): primitives read in the
	/// keyword family + builtin, user-defined types with their OWN declaration kind, `T` as typeParameter. The
	/// resolved type comes from the model's own type verb (the compiler is the source of truth); a single-segment
	/// name that resolves to NOTHING is not a primitive. First-wins in RecordOffset keeps this from clobbering a
	/// classification another node already recorded for the same offset.
	void ClassifyTypeSegments(const C4Compiler::QualTypeNode& qt, Ctx& ctx, const Core::Vector<const C4Compiler::AstNode*>& path)
	{
		if (!qt.BaseName())
			return;

		bool isPrimitive = false; // resolved through the model's own type verb; `char` of `const char*` too
		if (ctx.resolver)
		{
			const C4Compiler::Type* named = NamedType(ctx.resolver->ResolveQualType(qt, path));
			isPrimitive = named && named->IsPrimitive();
		}

		const auto& segments = qt.BaseName()->GetSegments();
		std::string prefix; // outermost..this segment, for namespace / type resolution
		for (uint32 i = 0; i < segments.Count(); ++i)
		{
			const auto& seg = segments[i];
			if (seg.nameToken.GetType() != C4Compiler::TokenType::Identifier)
				continue;
			const std::string name(seg.nameToken.GetText());
			prefix += (prefix.empty() ? "" : "::") + name;

			const bool endsPrimitive = isPrimitive && i + 1 == segments.Count();
			RecordOffset(ctx, seg.nameToken.GetStartLocation(), ClassifyTypeSegment(ctx, prefix, name, endsPrimitive, path));
		}
	}
} // namespace C4LSP::Features::TokenWalk
