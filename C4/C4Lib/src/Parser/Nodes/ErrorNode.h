#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace C4Compiler
{
	/// What a declaration or statement that did not parse leaves in the tree: the tokens the parser skipped to get to the
	/// next one, and why. The model and lowering pass it by; what comes after it parses on its own.
	struct ErrorNode : public AstNode
	{
		static constexpr NodeType kNodeType = NodeType::Error;

		ErrorNode(const SourceLocation& start, const SourceLocation& end, Core::String why);

		const Core::String& Why() const { return m_Why; }

		NodeType Type() const override { return kNodeType; }
		Core::String ToString() const override;

	private:
		Core::String m_Why;
	};
}
