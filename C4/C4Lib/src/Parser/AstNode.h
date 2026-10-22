#pragma once
#include "Lexer/Token.h"
#include "Core/Core.h"
#include "Compiler/ErrorTracker.h"
#include <span>

namespace Scripting
{
	enum class AccessModifier
	{
		Public,
		Private,
		Protected,
		None          // no explicit modifier
	};

	/// The modifier a `public` / `private` / `protected` token names; None for any other token.
	inline AccessModifier AccessModifierOf(TokenType type)
	{
		switch (type)
		{
		case TokenType::Public: return AccessModifier::Public;
		case TokenType::Private: return AccessModifier::Private;
		case TokenType::Protected: return AccessModifier::Protected;
		default: return AccessModifier::None;
		}
	}

	inline AccessModifier DefaultAccess(AccessModifier declared)
	{
		return declared == AccessModifier::None ? AccessModifier::Public : declared;
	}

	enum class NodeType
	{
		IncludeDeclaration,
		ExternDeclaration,
		Namespace,
		LiteralExpression,
		IdentifierExpression,
		BinaryExpression,
		UnaryExpression,
		TernaryExpression,
		FunctionCall,
		MemberAccess,
		StaticAccess,
		ArrayIndex,
		AssignmentExpression,
		ConditionalAnd,
		ConditionalOr,
		TypeCheck,
		SafeCast,
		CastExpression,
		TypeOfExpression,
		VaArgExpression,
		ParenthesizedExpression,
		NewExpression,
		Annotation,
		AnnotationItem,
		NamespaceQualifiedName,
		TemplateArgumentList,
		TemplateParameter,
		TemplateDefinition,
		QualType,
		Parameter,
		CodeBlock,
		ExpressionStatement,
		EmptyStatement,
		// Declaration nodes
		VariableDefinition,
		FunctionDefinition,
		ObjectDefinition,
		EnumDefinition,
		EnumMember,
		UnionDefinition,
		SwizzleView,
		AliasDefinition,
	FunPtrType,
	// Statement nodes
		IfStatement,
		WhileLoop,
		ForLoop,
		DoWhileLoop,
		SwitchStatement,
		SwitchCase,
		TryCatch,
		CatchClause,
		ReturnStatement,
		BreakStatement,
		ContinueStatement,
		DeferStatement,
		AsmStatement,
		FallthroughStatement,
		GotoStatement,
		LabelStatement,
	ThrowStatement,
	BracedInitializer,
	LambdaExpression,
	PackExpansion,
	FoldExpression,
	ValueList,
	Destructuring,
	Unknown
	};

	struct AstNode : public Refable
	{
		SourceLocation startLocation; // 1-based line/column of the first token
		SourceLocation endLocation;   // 1-based line/column after the last token

		AstNode() = default;
		virtual ~AstNode() = default;

		virtual NodeType Type() const { return NodeType::Unknown; }

		virtual std::span<const Ref<AstNode>> Children() const { return {}; }


		virtual Core::String ToString() const = 0;

	protected:
		Ref<AstNode> Shared() const { return Ref<AstNode>(const_cast<AstNode*>(this)); }
	};

	/// `node` as a `T` when it is one, by its kind (`T::kNodeType`), for a node class nothing derives from: a
	/// dynamic_cast walks the class hierarchy, and lowering asks of most nodes.
	template <typename T>
	const T* NodeAs(const AstNode* node) { return node && node->Type() == T::kNodeType ? static_cast<const T*>(node) : nullptr; }

	Core::String NodeTypeToName(NodeType type);

} // namespace Scripting
