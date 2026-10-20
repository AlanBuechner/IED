#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// A signature type: `funcptr<ReturnType(Parameters)>` (a function pointer) or `func<ReturnType(Parameters)>`
	/// (a callable value: docs/language/07). Examples: `funcptr<void(int32 code)>`, `func<int32(float x)>`.
	struct FunPtrTypeNode : public AstNode
	{
		FunPtrTypeNode() = default;

		static constexpr const char* kFuncKeyword = "func";

		/// `funcptr` or `func` followed by `<`.
		static bool StartsSignatureType(const Lexer& lexer);

		// --- Getters ---

		bool IsFunc() const { return m_IsFunc; }

		const Ref<QualTypeNode>& GetReturnType() const { return m_ReturnType; }
		uint32 ParameterCount() const { return m_Parameters.Count(); }
		const Core::Vector<Ref<ParameterNode>>& GetParameters() const { return m_Parameters; }

		static Ref<FunPtrTypeNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::FunPtrType; }

	private:
		bool m_IsFunc = false;
		Token m_FunPtrKeywordToken;
		Token m_OpenAngleToken;
		Ref<QualTypeNode> m_ReturnType;
		Token m_OpenParenToken;
		Core::Vector<Ref<ParameterNode>> m_Parameters;
		Token m_CloseParenToken;
		Token m_CloseAngleToken;
		Core::Vector<Ref<AstNode>> m_Children;
	};
}
