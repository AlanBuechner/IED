#include "Parser/Nodes/Expressions/PackExpansionNode.h"

namespace Scripting
{
	Ref<PackExpansionNode> PackExpansionNode::Around(const Ref<AstNode>& pattern, const Token& ellipsis)
	{
		auto node = CreateRef<PackExpansionNode>();
		node->m_Children.Push(pattern);
		node->m_Ellipsis = ellipsis;
		node->startLocation = pattern->startLocation;
		node->endLocation = ellipsis.GetEndLocation();
		return node;
	}

	Ref<FoldExpressionNode> FoldExpressionNode::Of(const Token& op, const Ref<AstNode>& first, const Ref<AstNode>& last,
		const SourceLocation& start, const SourceLocation& end)
	{
		auto node = CreateRef<FoldExpressionNode>();
		node->m_Operator = op;
		node->m_First = first;
		node->m_Last = last;
		if (first) node->m_Children.Push(first);
		if (last) node->m_Children.Push(last);
		node->startLocation = start;
		node->endLocation = end;
		return node;
	}

	Core::String FoldExpressionNode::ToString() const
	{
		const Core::String op = " " + m_Operator.GetText() + " ";
		Core::String text = "(";
		if (m_First) text += m_First->ToString() + op;
		text += "...";
		if (m_Last) text += op + m_Last->ToString();
		return text + ")";
	}
} // namespace Scripting
