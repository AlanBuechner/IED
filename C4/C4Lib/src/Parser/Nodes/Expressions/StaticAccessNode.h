#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Static/scope resolution access: baseType::memberName.
	struct StaticAccessNode : public AstNode
	{
		StaticAccessNode() = default;

		const Ref<AstNode>& GetBaseType() const { return m_BaseType; }
		const Token& GetScopeResolutionToken() const { return m_ScopeResolutionToken; }
		const Token& GetMemberNameToken() const { return m_MemberNameToken; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::StaticAccess; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_BaseType;
		Token m_ScopeResolutionToken; // '::'
		Token m_MemberNameToken;
	};
}
