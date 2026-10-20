#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h"

namespace Scripting
{
	/// `new T(args)` / `new T{args}` (a managed heap object) or `new (storage) T(args)` (the object is
	/// built in existing storage, docs/language/06). `new T[n]` / `new T[n]{...}` allocates n elements
	/// (docs/language/10); the type is then the element type and the count is a run-time value.
	struct NewExpressionNode : public AstNode
	{
		NewExpressionNode() = default;

		const Ref<QualTypeNode>& GetAllocType() const { return m_Type; }
		const Core::Vector<Ref<AstNode>>& GetArguments() const { return m_Arguments; }
		const Ref<AstNode>& GetPlacement() const { return m_Placement; } // null for a heap allocation
		bool HasBraces() const { return m_HasBraces; }
		bool IsArray() const { return m_ArrayCount != nullptr; }
		const Ref<AstNode>& GetArrayCount() const { return m_ArrayCount; }
		const Ref<BracedInitializerNode>& GetArrayInitializer() const { return m_ArrayInitializer; } // null without braces

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
		NodeType Type() const override { return NodeType::NewExpression; }

		void ParseBody(ParserState& state);

	private:
		bool ParsePlacement(ParserState& state);
		void ParseArguments(ParserState& state, TokenType close, const char* closeText);
		void ParseArrayRest(ParserState& state);
		void CollectChildren();

		Ref<QualTypeNode> m_Type;
		Ref<AstNode> m_Placement;
		Ref<AstNode> m_ArrayCount;
		Ref<BracedInitializerNode> m_ArrayInitializer;
		Core::Vector<Ref<AstNode>> m_Arguments;
		Core::Vector<Ref<AstNode>> m_Children; // the placement, the count, then the arguments or the initializer
		bool m_HasBraces = false;
	};
}
