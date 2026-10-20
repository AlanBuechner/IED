#include "UnaryExpressionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> UnaryExpressionNode::Children() const
	{
		if (m_Operand)
			return std::span<const Ref<AstNode>>(&const_cast<Ref<AstNode>&>(m_Operand), 1);
		return {};
	}

	Core::String UnaryExpressionNode::ToString() const
	{
		if (m_IsPostfix)
			return Core::StringFormat::Format("{0}{1}", m_Operand ? m_Operand->ToString() : "", m_OperatorToken.GetText());
		const Core::String operand = m_Operand ? m_Operand->ToString() : "";
		if (m_OperatorToken.GetType() == TokenType::Delete) return (m_IsArrayDelete ? "delete[] " : "delete ") + operand;
		return Core::StringFormat::Format("{0}{1}", m_OperatorToken.GetText(), operand);
	}

}
