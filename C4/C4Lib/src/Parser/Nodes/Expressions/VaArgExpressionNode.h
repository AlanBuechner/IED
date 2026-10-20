#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Scripting
{
	/// `va_arg(list, T)` (D-35): the next variadic argument, read as a `T`. The second operand is a type, so
	/// it parses like `sizeof`'s.
	struct VaArgExpressionNode : public AstNode
	{
		VaArgExpressionNode() = default;

		/// The intrinsic at the identifier `va_arg` followed by `(`; null otherwise.
		static Ref<VaArgExpressionNode> TryParse(ParserState& state);
		static bool StartsHere(ParserState& state);

		const Ref<AstNode>& GetList() const { return m_List; }
		const Ref<QualTypeNode>& GetArgumentType() const { return m_ArgumentType; }

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::VaArgExpression; }

	private:
		Ref<AstNode> m_List;
		Ref<QualTypeNode> m_ArgumentType;
		Core::Vector<Ref<AstNode>> m_Children;
	};
}
