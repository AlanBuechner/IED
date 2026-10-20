#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Comma-separated template arguments inside angle brackets.
	/// Created inline by NamespaceQualifiedNameNode; not parsed as a standalone construct.
	struct TemplateArgumentListNode : public AstNode
	{
		TemplateArgumentListNode() = default;

		// --- Getters ---

		const Token& GetOpenAngleToken() const { return m_OpenAngleToken; }
		const Core::Vector<Ref<AstNode>>& GetArguments() const { return m_Arguments; }
		const Token& GetCloseAngleToken() const { return m_CloseAngleToken; }
		uint32 ArgumentCount() const { return m_Arguments.Count(); }

		std::span<const Ref<AstNode>> Children() const override { return m_Arguments; }
		Core::String ToString() const override;

		/// Build an argument-list node from already-parsed parts without re-reading the lexer. Used by
		/// template substitution to rebuild a specialization's `<...>` with its arguments rewritten.
		static Ref<TemplateArgumentListNode> FromArguments(const Token& openAngle, const Core::Vector<Ref<AstNode>>& args, const Token& closeAngle);

	protected:
		NodeType Type() const override { return NodeType::TemplateArgumentList; }

	private:
		friend struct NamespaceQualifiedNameNode;
		Token m_OpenAngleToken;
		Core::Vector<Ref<AstNode>> m_Arguments;
		Token m_CloseAngleToken;
	};

	/// A namespace-qualified name with optional template arguments at each segment.
	///
	/// Example: `NC::SomeClass<float>::SubType<NC::Other<int>>::Type3`
	struct NamespaceQualifiedNameNode : public AstNode
	{
		struct Segment
		{
			Token nameToken;
			Ref<TemplateArgumentListNode> templateArgs; // nullptr if absent
		};

		NamespaceQualifiedNameNode() = default;

		// --- Getters ---

		const Core::Vector<Segment>& GetSegments() const { return m_Segments; }
		uint32 SegmentCount() const { return m_Segments.Count(); }
		const Token& GetFirstIdentifierToken() const;

		static Ref<NamespaceQualifiedNameNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

		/// Build a qualified name from already-parsed parts (a name token plus optional template args)
		/// without re-reading the lexer. Used by the expression parser when `ident<args>` appears in
		/// expression position (composite literal or static member access), where the tokens are
		/// already consumed and must not be re-parsed.
		static Ref<NamespaceQualifiedNameNode> FromParts(const Token& nameToken, const Ref<TemplateArgumentListNode>& tmplArgs);

		/// Build a qualified name from an already-parsed run of segments without re-reading the lexer.
		/// Used by the expression parser for a QUALIFIED instantiation prefix (`Math::Vec2<float>{...}`),
		/// where the leading segments are consumed one at a time and must stop before a trailing
		/// `::member` chain (which ParsePostfix re-chains onto).
		static Ref<NamespaceQualifiedNameNode> FromSegments(const Core::Vector<Segment>& segments);

		Core::String ToString() const override;

	public:
		/// Parse `< arg (, arg)* >` into a TemplateArgumentListNode. Exposed so the Pratt
		/// parser can reuse it for explicit function-template call sites (`Foo<int>(...)`).
		Ref<TemplateArgumentListNode> ParseTemplateArguments(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::NamespaceQualifiedName; }

	private:
		Core::Vector<Segment> m_Segments;

		/// Parse one segment: identifier followed by optional template arguments.
		void ParseSegment(ParserState& state, Segment& outSegment);
	};
}
