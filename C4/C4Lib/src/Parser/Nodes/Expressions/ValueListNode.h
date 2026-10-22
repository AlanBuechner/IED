#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// `[e0, e1, ...]` (D-34): the values a function returns together (`return [1.0f, true];`), or the right side
	/// of a destructuring (`[a, b] = [b, a];`). An element may be a pack expansion (`[args...]`).
	struct ValueListNode : public AstNode
	{
		static constexpr NodeType kNodeType = NodeType::ValueList;
		static Ref<ValueListNode> Parse(ParserState& state);

		const Core::Vector<Ref<AstNode>>& GetElements() const { return m_Elements; }

		std::span<const Ref<AstNode>> Children() const override { return m_Elements; }
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::ValueList; }

	private:
		Core::Vector<Ref<AstNode>> m_Elements;
	};
} // namespace Scripting
