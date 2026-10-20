#include "ObjectDefinitionNode.h"
#include "AnnotationNode.h"
#include "QualTypeNode.h"
#include "TemplateDefinitionNode.h"
#include "VariableDefinitionNode.h"
#include "FunctionDefinitionNode.h"
#include "EnumDefinitionNode.h"
#include "UnionDefinitionNode.h"
#include "SwizzleViewNode.h"
#include "AliasDefinitionNode.h"
#include "../Statements/CodeBlockNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	static bool IsAccessModifier(TokenType type)
	{
		return type == TokenType::Public || type == TokenType::Private || type == TokenType::Protected;
	}

	static bool IsClassModifier(const Token& token)
	{
		return token.GetType() == TokenType::Identifier && (token.GetText() == "abstract" || token.GetText() == "final");
	}

	/// Default member visibility for a type kind: classes are private by default,
	/// structs and interfaces are public by default.
	static AccessModifier DefaultMemberAccess(ObjectKind kind)
	{
		switch (kind) {
			case ObjectKind::Class: return AccessModifier::Private;
			case ObjectKind::Struct:
			case ObjectKind::Interface:
			default: return AccessModifier::Public;
		}
	}

	/// True if the given node is a nested type definition (class/struct/interface). Nested
	/// types are sub-scopes, not plain members, so they are exempt from section requirements.
	static bool IsNestedTypeNode(const Ref<AstNode>& node)
	{
		if (!node) return false;
		NodeType t = node->Type();
		return t == NodeType::ObjectDefinition || t == NodeType::UnionDefinition;
	}

	/// Read the explicit access modifier a declaration node carries (None if it wrote none).
	static AccessModifier GetMemberExplicitAccess(const Ref<AstNode>& node)
	{
		switch (node->Type()) {
			case NodeType::VariableDefinition: return static_cast<const VariableDefinitionNode&>(*node).GetAccessModifier();
			case NodeType::FunctionDefinition: return static_cast<const FunctionDefinitionNode&>(*node).GetAccessModifier();
			case NodeType::EnumDefinition:     return static_cast<const EnumDefinitionNode&>(*node).GetAccessModifier();
			case NodeType::UnionDefinition:    return static_cast<const UnionDefinitionNode&>(*node).GetAccessModifier();
			case NodeType::AliasDefinition:    return static_cast<const AliasDefinitionNode&>(*node).GetAccessModifier();
			default: return AccessModifier::None;
		}
	}

	/// Store a resolved (effective) access modifier back onto a declaration node so the
	/// semantic layer sees the section-resolved visibility, not just the explicit keyword.
	static void SetMemberAccess(Ref<AstNode> node, AccessModifier access)
	{
		switch (node->Type()) {
			case NodeType::VariableDefinition: static_cast<VariableDefinitionNode&>(*node).SetAccessModifier(access); break;
			case NodeType::FunctionDefinition: static_cast<FunctionDefinitionNode&>(*node).SetAccessModifier(access); break;
			case NodeType::EnumDefinition:     static_cast<EnumDefinitionNode&>(*node).SetAccessModifier(access); break;
			case NodeType::UnionDefinition:    static_cast<UnionDefinitionNode&>(*node).SetAccessModifier(access); break;
			case NodeType::AliasDefinition:    static_cast<AliasDefinitionNode&>(*node).SetAccessModifier(access); break;
			default: break; // nested types / other nodes keep their own (or no) modifier
		}
	}

	/// The name a diagnostic gives a member.
	static Core::String MemberName(const Ref<AstNode>& node)
	{
		switch (node->Type()) {
			case NodeType::VariableDefinition: return static_cast<const VariableDefinitionNode&>(*node).GetNameToken().GetText();
			case NodeType::FunctionDefinition: return static_cast<const FunctionDefinitionNode&>(*node).GetName()->ToString();
			case NodeType::EnumDefinition:     return static_cast<const EnumDefinitionNode&>(*node).GetNameToken().GetText();
			case NodeType::AliasDefinition:    return static_cast<const AliasDefinitionNode&>(*node).GetNameToken().GetText();
			default: return node->ToString();
		}
	}

	/// Human-readable keyword for an access modifier (used in diagnostics).
	static const char* SectionName(AccessModifier access)
	{
		switch (access) {
			case AccessModifier::Public: return "public";
			case AccessModifier::Private: return "private";
			case AccessModifier::Protected: return "protected";
			default: return "none";
		}
	}

	static bool IsHidden(AccessModifier access)
	{
		return access == AccessModifier::Private || access == AccessModifier::Protected;
	}

	/// Why a member's written modifier does not fit the section it is under (D-28), or empty.
	static Core::String SectionMismatch(const Ref<AstNode>& node, AccessModifier written, AccessModifier section)
	{
		if (section == AccessModifier::None || written == section) return "";
		if (written == AccessModifier::None)
			return "member '" + MemberName(node) + "' is under '" + SectionName(section) + ":' but has no access modifier; write '" +
				SectionName(section) + "'";
		return Core::StringFormat::Format("Member declares '{0}' but is inside a '{1}:' section", SectionName(written), SectionName(section));
	}

	static AccessModifier EffectiveAccess(ObjectKind kind, AccessModifier written, AccessModifier section)
	{
		if (written != AccessModifier::None) return written;
		return section != AccessModifier::None ? section : DefaultMemberAccess(kind);
	}

	/// Check a freshly-parsed body member against its section and its type kind, and record its visibility on
	/// the node. A section label requires every member under it to write the matching modifier (D-28); an
	/// interface member is always public. Nested types are sub-scopes and are exempt.
	static void ApplyAccessSection(Ref<AstNode> node, ObjectKind kind, AccessModifier currentSection, ErrorTracker& errorTracker)
	{
		if (!node || IsNestedTypeNode(node)) return;
		const AccessModifier written = GetMemberExplicitAccess(node);
		if (Core::String why = SectionMismatch(node, written, currentSection); !why.empty())
			errorTracker.AddError(node->startLocation, why, ErrorSeverity::Error);
		if (kind == ObjectKind::Interface && IsHidden(written))
			errorTracker.AddError(node->startLocation, "interface member '" + MemberName(node) + "' cannot be '" + SectionName(written) +
				"': an interface's members are public", ErrorSeverity::Error);
		SetMemberAccess(node, EffectiveAccess(kind, written, currentSection));
	}

	std::span<const Ref<AstNode>> ObjectDefinitionNode::Children() const
	{
		return m_Children;
	}

	Core::String ObjectDefinitionNode::ToString() const
	{
		Core::String result;

		if (m_IsAbstract)
			result += "abstract ";
		if (m_IsFinal)
			result += "final ";

		switch (m_Kind) {
			case ObjectKind::Class: result += "class"; break;
			case ObjectKind::Struct: result += "struct"; break;
			case ObjectKind::Interface: result += "interface"; break;
		}

		if (!m_NameToken.GetText().empty())
		{
			result += " ";
			result += m_NameToken.GetText();
		}

		if (m_HasInheritanceList)
		{
			result += " : ";
			for (uint32 i = 0; i < m_BaseClasses.Count(); ++i)
			{
				if (i > 0)
					result += ", ";
				switch (m_BaseClasses[i].m_AccessModifier) {
					case InheritanceModifier::Public: result += "public "; break;
					case InheritanceModifier::Private: result += "private "; break;
					case InheritanceModifier::Protected: result += "protected "; break;
					default: break;
				}
				if (m_BaseClasses[i].m_Type)
					result += m_BaseClasses[i].m_Type->ToString();
			}
		}

		return result;
	}

	Ref<ObjectDefinitionNode> ObjectDefinitionNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token first = lexer.PeekCompound();
		if (first.GetType() != TokenType::OpenBracket &&
			first.GetType() != TokenType::Class &&
			first.GetType() != TokenType::Struct &&
			first.GetType() != TokenType::Interface &&
			!IsAccessModifier(first.GetType()) &&
			first.GetType() != TokenType::Template &&
			!IsClassModifier(first))
		{
			return nullptr;
		}

	auto undo = LexerUndoScope(lexer);
	state.GetErrorTracker().SuppressErrors();

	auto node = CreateRef<ObjectDefinitionNode>();
	node->ParseBody(state);

	// Named types succeed on the name; anonymous definitions (`struct { ... };`) on the opened body.
	if (!node->m_NameToken.GetText().empty() || node->HasBody())
	{
		state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return node;
	}

	state.GetErrorTracker().DiscardSuppressedErrors();
	return nullptr;
	}

	void ObjectDefinitionNode::ParseClassModifiers(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		for (Token next = lexer.PeekCompound(); IsClassModifier(next); next = lexer.PeekCompound())
		{
			const bool isAbstract = next.GetText() == "abstract";
			if (isAbstract ? m_IsAbstract : m_IsFinal)
				state.GetErrorTracker().AddError(next.GetStartLocation(), "'" + next.GetText() + "' is written twice", ErrorSeverity::Error);
			(isAbstract ? m_IsAbstract : m_IsFinal) = true;
			(isAbstract ? m_AbstractToken : m_FinalToken) = next;
			if (startLocation.line == 0)
				startLocation = next.GetStartLocation();
			lexer.NextCompoundToken();
		}
	}

	void ObjectDefinitionNode::CheckClassModifiers(ErrorTracker& errorTracker) const
	{
		if (m_IsAbstract && m_IsFinal)
			errorTracker.AddError(m_FinalToken.GetStartLocation(), "a type cannot be both 'abstract' and 'final': nothing could create it", ErrorSeverity::Error);
		if (m_Kind != ObjectKind::Interface) return;
		if (m_IsAbstract)
			errorTracker.AddError(m_AbstractToken.GetStartLocation(), "an interface is always abstract: remove 'abstract'", ErrorSeverity::Error);
		if (m_IsFinal)
			errorTracker.AddError(m_FinalToken.GetStartLocation(), "an interface cannot be 'final': it exists to be implemented", ErrorSeverity::Error);
	}

	void ObjectDefinitionNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();
		TypeParameterScope typeParameters(state);

		m_Annotations.Clear();
		m_AccessModifier = AccessModifier::None;
		m_IsAbstract = false;
		m_IsFinal = false;
		m_Kind = ObjectKind::Class;
		m_TemplateDefinition = nullptr;
		m_SpecializationArguments = nullptr;
		m_HasInheritanceList = false;
		m_BaseClasses.Clear();
		m_BodyMembers.Clear();

		// 1. Parse annotations
		while (true)
		{
			Token t = lexer.PeekCompound();
			if (t.GetType() != TokenType::OpenBracket)
				break;

			auto annotation = CreateRef<AnnotationNode>();
			annotation->ParseBody(state);

			if (!annotation->GetBracketGroups().Empty())
			{
				m_Annotations.Push(annotation);
				if (startLocation.line == 0)
					startLocation = annotation->startLocation;
			}
			else
			{
				errorTracker.AddError(t.GetStartLocation(), "Expected valid annotation", ErrorSeverity::Error);
				break;
			}
		}

		// 2. Access modifier
		Token next = lexer.PeekCompound();
		if (IsAccessModifier(next.GetType()))
		{
			m_AccessModifier = AccessModifierOf(next.GetType());
			m_AccessModifierToken = next;
			if (startLocation.line == 0)
				startLocation = next.GetStartLocation();
			lexer.NextCompoundToken();
			next = lexer.PeekCompound();
		}

		// 3. Optional template definition - PREFIX before abstract/class/struct/interface
		if (next.GetType() == TokenType::Template)
		{
			m_TemplateDefinition = CreateRef<TemplateDefinitionNode>();
			m_TemplateDefinition->ParseBody(state);
			m_TemplateDefinition->BringTypeParametersInto(typeParameters);
			next = lexer.PeekCompound();
		}

		// 2b. Access modifier after template (handles `template<T> public class`)
		if (IsAccessModifier(next.GetType()))
		{
			m_AccessModifier = AccessModifierOf(next.GetType());
			m_AccessModifierToken = next;
			if (startLocation.line == 0)
				startLocation = next.GetStartLocation();
			lexer.NextCompoundToken();
			next = lexer.PeekCompound();
		}

		// 4. abstract / final
		ParseClassModifiers(state);
		next = lexer.PeekCompound();

		// 5. Object kind keyword
		if (next.GetType() == TokenType::Class ||
			next.GetType() == TokenType::Struct ||
			next.GetType() == TokenType::Interface)
		{
			switch (next.GetType()) {
				case TokenType::Class: m_Kind = ObjectKind::Class; break;
				case TokenType::Struct: m_Kind = ObjectKind::Struct; break;
				case TokenType::Interface: m_Kind = ObjectKind::Interface; break;
				default: break;
			}
			m_ObjectKindToken = next;
			if (startLocation.line == 0)
				startLocation = next.GetStartLocation();
			lexer.NextCompoundToken();
			CheckClassModifiers(errorTracker);
		}
		else
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected 'class', 'struct', or 'interface', got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			return;
		}

	// 6. Name (optional: anonymous definitions `struct { ... };` are allowed, e.g. swizzle views)
	Token nameToken = lexer.PeekCompound();
	if (nameToken.GetType() == TokenType::Identifier)
	{
		m_NameToken = nameToken;
		lexer.NextCompoundToken();
	}

	// A specialization names the arguments it is for: `template<> struct Box<bool>`, `template<T> struct Box<T*>`.
	if (m_TemplateDefinition && lexer.PeekCompound().GetType() == TokenType::Less)
		m_SpecializationArguments = NamespaceQualifiedNameNode().ParseTemplateArguments(state);

		// 7. Optional inheritance list
		Token maybeColon = lexer.PeekCompound();
		if (maybeColon.GetType() == TokenType::Colon)
		{
			m_HasInheritanceList = true;
			m_ColonToken = maybeColon;
			lexer.NextCompoundToken();

			while (true)
			{
				BaseClassEntry entry;

				Token baseAccess = lexer.PeekCompound();
				if (IsAccessModifier(baseAccess.GetType()))
				{
					switch (baseAccess.GetType()) {
						case TokenType::Public: entry.m_AccessModifier = InheritanceModifier::Public; break;
						case TokenType::Private: entry.m_AccessModifier = InheritanceModifier::Private; break;
						case TokenType::Protected: entry.m_AccessModifier = InheritanceModifier::Protected; break;
						default: break;
					}
					entry.m_AccessModifierToken = baseAccess;
					lexer.NextCompoundToken();
				}

				auto baseType = CreateRef<QualTypeNode>();
				baseType->ParseBody(state);
				entry.m_Type = baseType;

				m_BaseClasses.Push(entry);

				Token afterBase = lexer.PeekCompound();
				if (afterBase.GetType() == TokenType::Comma)
				{
					lexer.NextCompoundToken();
				}
				else
				{
					break;
				}
			}
		}

	// 8. Opening brace OR a forward declaration (`class Window;`). A `;` right after a NAMED type
	// with no inheritance list is a declaration-only forward declaration - it introduces the name
	// as an incomplete type so pointers/references and later definitions can use it (C++ fwd-decl).
	Token openBrace = lexer.PeekCompound();
	if (openBrace.GetType() == TokenType::OpenCurl)
	{
		m_OpenBraceToken = openBrace;
		m_HasOpenBrace = true;
		lexer.NextCompoundToken();
	}
	else if (openBrace.GetType() == TokenType::SemiColon && !m_NameToken.GetText().empty() && !m_HasInheritanceList)
	{
		m_IsDeclaration = true;
		m_SemicolonToken = openBrace;
		lexer.NextCompoundToken();
		return; // no body to parse
	}
		else
		{
			errorTracker.AddError(
				openBrace.GetStartLocation(),
				Core::StringFormat::Format("Expected an opening brace for the type body, got '{0}'", openBrace.GetText()),
				ErrorSeverity::Error);
			return;
		}

		// 9. Parse body members until closing brace
		//
		// Access sections: a `public:` / `private:` / `protected:` label opens a section that
		// REQUIRES every plain member beneath it to carry a matching explicit modifier. The
		// label groups members (so you can see the region at a glance) but never grants default
		// visibility on its own - each member still states its own access, and it must agree with
		// the section. Members before any label use the type's default (class -> private,
		// struct/interface -> public). Nested types are sub-scopes and are exempt.
		AccessModifier currentSection = AccessModifier::None; // None = no active section label

		while (true)
		{
			Token t = lexer.PeekCompound();

			if (t.GetType() == TokenType::EndOfFile || t.GetType() == TokenType::CloseCurl)
				break;

			// Access section label: `public:` / `private:` / `protected:`
			if (IsAccessModifier(t.GetType()) && lexer.PeekCompound(1).GetType() == TokenType::Colon)
			{
				currentSection = AccessModifierOf(t.GetType());
				if (m_Kind == ObjectKind::Interface && IsHidden(currentSection))
					errorTracker.AddError(t.GetStartLocation(), Core::String("an interface cannot have a '") + SectionName(currentSection) +
						":' section: its members are public", ErrorSeverity::Error);
				lexer.NextCompoundToken(); // consume the keyword
				lexer.NextCompoundToken(); // consume ':'
				continue;
			}

#define TRY_PARSE_MEMBER(type) \
			{ auto node = type::TryParse(state); \
			if (node != nullptr) { ApplyAccessSection(node, m_Kind, currentSection, errorTracker); m_BodyMembers.Push(node); continue; } }

	TRY_PARSE_MEMBER(FunctionDefinitionNode);
	TRY_PARSE_MEMBER(VariableDefinitionNode);
	TRY_PARSE_MEMBER(SwizzleViewNode);
			TRY_PARSE_MEMBER(ObjectDefinitionNode);
			TRY_PARSE_MEMBER(EnumDefinitionNode);
			TRY_PARSE_MEMBER(UnionDefinitionNode);
			TRY_PARSE_MEMBER(AliasDefinitionNode);

#undef TRY_PARSE_MEMBER

			errorTracker.AddError(
				t.GetStartLocation(),
				Core::StringFormat::Format("Unexpected token '{0}' in type body", t.GetText()),
				ErrorSeverity::Error);
			lexer.NextCompoundToken();
		}

	// Methods must be processed after every field/type so a member declared after its type's
	// methods (C++-style trailing `private:` data section) is still visible to method bodies and
	// laid out. Stable-partition body members: all non-function declarations first, in their
	// original relative order, then the function (method) definitions in original order. Keeping
	// the declarative group in source-relative order preserves layout (a union interleaved with
	// plain fields keeps its declared position, so CalculateLayout offsets match source order).
	if (m_BodyMembers.Count() > 1)
	{
		Core::Vector<Ref<AstNode>> reordered;
		for (const auto& member : m_BodyMembers)
			if (member && member->Type() != NodeType::FunctionDefinition)
				reordered.Push(member);
		for (const auto& member : m_BodyMembers)
			if (member && member->Type() == NodeType::FunctionDefinition)
				reordered.Push(member);
		m_BodyMembers = reordered;
	}

	// 10. Closing brace
	Token closeBrace = lexer.PeekCompound();
	if (closeBrace.GetType() == TokenType::CloseCurl)
	{
		m_CloseBraceToken = closeBrace;
		endLocation = closeBrace.GetEndLocation();
		lexer.NextCompoundToken();
	}
	else if (closeBrace.GetType() != TokenType::EndOfFile)
	{
		errorTracker.AddError(
			closeBrace.GetStartLocation(),
			Core::StringFormat::Format("Expected '}}' to close type body, got '{0}'", closeBrace.GetText()),
			ErrorSeverity::Error);
		endLocation = startLocation;
	}

	// 11. Optional trailing semicolon
	Token semi = lexer.PeekCompound();
	if (semi.GetType() == TokenType::SemiColon)
	{
		m_SemicolonToken = semi;
		endLocation = semi.GetEndLocation();
		lexer.NextCompoundToken();
	}

	if (endLocation.line == 0)
		endLocation = startLocation;

		m_Children.Clear();
			for (uint32 i = 0; i < m_Annotations.Count(); ++i)
				m_Children.Push(m_Annotations[i]);
			if (m_TemplateDefinition)
				m_Children.Push(m_TemplateDefinition);
			if (m_SpecializationArguments)
				m_Children.Push(m_SpecializationArguments);
			for (uint32 i = 0; i < m_BodyMembers.Count(); ++i)
				m_Children.Push(m_BodyMembers[i]);
	}


} // namespace Scripting
