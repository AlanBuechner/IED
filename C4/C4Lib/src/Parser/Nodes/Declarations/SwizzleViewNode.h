#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"
#include "QualTypeNode.h"

namespace Scripting
{
	/// One packed/unpacked lane view inside a class/struct (or its anonymous union):
	///   packed T x, y;      -> SIMD <2 x T> region with named lanes x(0), y(1)
	///   unpacked T x, y;    -> byte-aliased struct fields (no runs)
	/// A `packed` view is the HLSL-style vector: its lanes are accessed with dot runs (v.xy) and
	/// lower to shufflevector. Multiple packed views in one union that agree on element type +
	/// lane count ALIAS the same SIMD region, so x==r when both name index 0.
	struct SwizzleViewNode : public AstNode
	{
		SwizzleViewNode() = default;

		bool IsPacked() const { return m_Packed; }
		const Ref<QualTypeNode>& GetElementType() const { return m_ElementType; }
		const Core::Vector<Token>& GetLanes() const { return m_Lanes; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;

		/// Parses `packed|unpacked Type ident (',' ident)* ';'` when the next token is one of the
		/// two keywords; returns nullptr otherwise (so callers can fall through to other member forms).
		static Ref<SwizzleViewNode> TryParse(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::SwizzleView; }

	private:
		bool m_Packed = true;
		Ref<QualTypeNode> m_ElementType;
		Core::Vector<Token> m_Lanes;
	};
} // namespace Scripting
