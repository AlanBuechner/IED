#pragma once
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"

namespace C4LSP::Server
{
	/// Calls `visit` with each node written inside `node`, for the walks over code as it is written: its children, and
	/// what the compiler keeps out of them - the types of an object's base list (BUG-390), a lambda's init captures
	/// as the variables they declare, its parameters, return type and body, which are a scope of their own (BUG-403),
	/// and the template arguments of a name's segments and of a call (`Ref<T>`, `Get<T>()`, BUG-472).
	template <typename Visit>
	void ForEachWrittenChild(const C4Compiler::AstNode& node, Visit&& visit)
	{
		if (node.Type() == C4Compiler::NodeType::LambdaExpression)
		{
			const auto& lambda = static_cast<const C4Compiler::LambdaNode&>(node);
			for (const C4Compiler::LambdaCapture& capture : lambda.GetCaptures())
				if (capture.declaration)
					visit(static_cast<const C4Compiler::AstNode&>(*capture.declaration.Get()));
			for (const auto& parameter : lambda.GetParameters())
				if (parameter)
					visit(static_cast<const C4Compiler::AstNode&>(*parameter.Get()));
			if (lambda.GetReturnType())
				visit(*lambda.GetReturnType().Get());
			if (lambda.GetBody())
				visit(*lambda.GetBody().Get());
			return;
		}
		for (const auto& child : node.Children())
			if (child)
				visit(*child.Get());
		if (node.Type() == C4Compiler::NodeType::ObjectDefinition)
			for (const C4Compiler::BaseClassEntry& base : static_cast<const C4Compiler::ObjectDefinitionNode&>(node).GetBaseClasses())
				if (base.m_Type)
					visit(static_cast<const C4Compiler::AstNode&>(*base.m_Type.Get()));
		if (node.Type() == C4Compiler::NodeType::NamespaceQualifiedName)
			for (const C4Compiler::NamespaceQualifiedNameNode::Segment& segment : static_cast<const C4Compiler::NamespaceQualifiedNameNode&>(node).GetSegments())
				if (segment.templateArgs)
					visit(static_cast<const C4Compiler::AstNode&>(*segment.templateArgs.Get()));
		if (node.Type() == C4Compiler::NodeType::FunctionCall)
			if (const auto& arguments = static_cast<const C4Compiler::FunctionCallNode&>(node).GetTemplateArgs())
				visit(static_cast<const C4Compiler::AstNode&>(*arguments.Get()));
	}
} // namespace C4LSP::Server
