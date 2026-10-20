#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Scripting
{
	/// `new T(args)` / `new T{args}` (a managed heap object) or `new (storage) T(args)` (the object is
	/// built in existing storage, docs/language/06).
	struct NewExpressionNode : public AstNode
	{
		NewExpressionNode() = default;

		const Ref<QualTypeNode>& GetAllocType() const { return m_Type; }
		const Core::Vector<Ref<AstNode>>& GetArguments() const { return m_Arguments; }
		const Ref<AstNode>& GetPlacement() const { return m_Placement; } // null for a heap allocation
		bool HasBraces() const { return m_HasBraces; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
		NodeType Type() const override { return NodeType::NewExpression; }

		void ParseBody(ParserState& state);

	private:
		bool ParsePlacement(ParserState& state);
		void ParseArguments(ParserState& state, TokenType close, const char* closeText);
		void CollectChildren();

		Ref<QualTypeNode> m_Type;
		Ref<AstNode> m_Placement;
		Core::Vector<Ref<AstNode>> m_Arguments;
		Core::Vector<Ref<AstNode>> m_Children; // the placement, then the arguments
		bool m_HasBraces = false;
	};
}
