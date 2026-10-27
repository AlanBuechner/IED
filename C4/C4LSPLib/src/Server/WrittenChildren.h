#pragma once
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"

namespace C4LSP::Server
{
	/// Calls `visit` with each node written inside `node`, for the walks over code as it is written: its children, and
	/// what the compiler keeps out of them - the types of an object's base list (BUG-390), and a lambda's init captures
	/// as the variables they declare, its parameters, return type and body, which are a scope of their own (BUG-403).
	template <typename Visit>
	void ForEachWrittenChild(const Scripting::AstNode& node, Visit&& visit)
	{
		if (node.Type() == Scripting::NodeType::LambdaExpression)
		{
			const auto& lambda = static_cast<const Scripting::LambdaNode&>(node);
			for (const Scripting::LambdaCapture& capture : lambda.GetCaptures())
				if (capture.declaration)
					visit(static_cast<const Scripting::AstNode&>(*capture.declaration.Get()));
			for (const auto& parameter : lambda.GetParameters())
				if (parameter)
					visit(static_cast<const Scripting::AstNode&>(*parameter.Get()));
			if (lambda.GetReturnType())
				visit(*lambda.GetReturnType().Get());
			if (lambda.GetBody())
				visit(*lambda.GetBody().Get());
			return;
		}
		for (const auto& child : node.Children())
			if (child)
				visit(*child.Get());
		if (node.Type() == Scripting::NodeType::ObjectDefinition)
			for (const Scripting::BaseClassEntry& base : static_cast<const Scripting::ObjectDefinitionNode&>(node).GetBaseClasses())
				if (base.m_Type)
					visit(static_cast<const Scripting::AstNode&>(*base.m_Type.Get()));
	}
} // namespace C4LSP::Server
