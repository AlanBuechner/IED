#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Member access expression: baseObject.memberName (dot or arrow).
	struct MemberAccessNode : public AstNode
	{
		static constexpr NodeType kNodeType = NodeType::MemberAccess;
		MemberAccessNode() = default;

		const Ref<AstNode>& GetBaseObject() const { return m_BaseObject; }
		const Token& GetAccessOperatorToken() const { return m_AccessOperatorToken; }
		const Token& GetMemberNameToken() const { return m_MemberNameToken; }
		bool IsArrowAccess() const;
		/// `obj.Base::F()`: the names written between the access operator and the member, `Base` here (B35).
		const Core::Vector<Token>& GetQualifier() const { return m_Qualifier; }
		bool IsQualified() const { return !m_Qualifier.Empty(); }
		/// The qualifier joined with `::` (`Ns::Base`).
		Core::String QualifierText() const;

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::MemberAccess; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_BaseObject;
		Token m_AccessOperatorToken; // '.' or '->'
		Token m_MemberNameToken;
		Core::Vector<Token> m_Qualifier;
	};
}
