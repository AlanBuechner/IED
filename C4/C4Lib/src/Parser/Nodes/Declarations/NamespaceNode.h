#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Represents a namespace declaration in C4 source.
	///
	/// Example: `namespace MyNamespace { ... }`
	///
	/// When `isRoot` is true this represents the implicit file-level namespace
	/// that wraps all top-level declarations (includes, namespaces, classes)
	/// without consuming an identifier or braces.
	struct NamespaceNode : public AstNode
	{
		NamespaceNode() = default;

		// --- Getters ---

		const Core::String& Name() const { return m_NameToken.GetText(); }
		const Token& GetNameToken() const { return m_NameToken; }
		bool IsRoot() const { return m_IsRoot; }
		void SetIsRoot(bool value) { m_IsRoot = value; }

		static Ref<NamespaceNode> TryParse(ParserState& state);

		void ParseBody(ParserState& state);

		std::span<const Ref<AstNode>> Children() const override { return m_ChildNodes; }

		/// Mutable access to child nodes - used by include AST merging.
		Core::Vector<Ref<AstNode>>& GetMutableChildren() { return m_ChildNodes; }

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::Namespace; }

	private:
		// The `::B::C` after `namespace A`, one node per name, outermost first.
		static Core::Vector<Ref<NamespaceNode>> ParseNestedNames(ParserState& state);

		Token m_NameToken;                        // namespace identifier token (default-constructed for root)
		bool m_IsRoot = false;                    // true for implicit file-level namespace
		Core::Vector<Ref<AstNode>> m_ChildNodes;  // nested declarations
	};
}
