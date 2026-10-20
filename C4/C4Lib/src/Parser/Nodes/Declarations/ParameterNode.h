#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Core/Core.h"

namespace Scripting
{
	struct QualTypeNode;

	/// Represents a function parameter including optional annotations, type, name, and default value.
	///
	/// Grammar: annotation* type [identifier [= expression]]
	/// Examples: `int x`, `const String& name`, `[In] float value = 0.0f`
	struct ParameterNode : public AstNode
	{
		ParameterNode() = default;

		// --- Getters ---

		const Core::Vector<Ref<AnnotationNode>>& GetAnnotations() const { return m_Annotations; }
		const Ref<QualTypeNode>& GetType() const { return m_Type; }
		const Token& GetNameToken() const { return m_NameToken; }
		const Ref<AstNode>& GetDefaultValue() const { return m_DefaultValue; }

		bool HasName() const { return !m_NameToken.GetText().empty(); }
		/// `Ts... args`: a function parameter pack, one parameter per element of the packs its type names (D-19).
		bool IsPack() const { return m_IsPack; }
		bool HasDefaultValue() const { return m_DefaultValue != nullptr; }

		std::span<const Ref<AstNode>> Children() const override; // annotations + type + default value
		Core::String ToString() const override;
static Ref<ParameterNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::Parameter; }

	private:
		Core::Vector<Ref<AnnotationNode>> m_Annotations;
		Ref<QualTypeNode> m_Type;
		bool m_IsPack = false;
		Token m_NameToken;
		Ref<AstNode> m_DefaultValue;
		Core::Vector<Ref<AstNode>> m_Children; // flattened: annotations + type + default value
	};
}
