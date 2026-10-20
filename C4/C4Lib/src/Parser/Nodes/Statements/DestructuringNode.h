#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// `[float a, b, _] = value;` (D-34): each value of a return list goes to its element, in order - a new
	/// variable (`float a`), an existing location (`b`, `p.x`), or nowhere (`_`). The right side is evaluated
	/// first, so `[a, b] = [b, a];` swaps.
	struct DestructuringNode : public AstNode
	{
		struct Element
		{
			enum class Kind { Declare, Assign, Discard };
			Kind kind = Kind::Discard;
			Ref<QualTypeNode> type; // Declare
			Token name;             // Declare; the `_` of a Discard
			Ref<AstNode> target;    // Assign
		};

		static Ref<DestructuringNode> TryParse(ParserState& state);

		const Core::Vector<Element>& GetElements() const { return m_Elements; }
		const Ref<AstNode>& GetValue() const { return m_Value; }

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::Destructuring; }

	private:
		static bool Ahead(const Lexer& lexer);
		void ParseBody(ParserState& state);
		Element ParseElement(ParserState& state);

		Core::Vector<Element> m_Elements;
		Ref<AstNode> m_Value;
		Core::Vector<Ref<AstNode>> m_Children;
	};
} // namespace Scripting
