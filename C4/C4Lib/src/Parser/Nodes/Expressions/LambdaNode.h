#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"
#include "../Declarations/ParameterNode.h"

namespace Scripting
{
	/// What a lambda captures when the body uses an enclosing local the list does not name.
	enum class CaptureDefault
	{
		None,        // `[]`, `[x, &y]`
		ByValue,     // `[=]`
		ByReference  // `[&]`
	};

	/// One entry of a capture list (docs/language/07, D-22).
	struct LambdaCapture
	{
		enum class Kind
		{
			Variable,   // `x` / `&x`
			Init,       // `n = expr` / `&r = expr`
			This,       // `this`
			ThisCopy    // `*this`
		};
		Kind kind = Kind::Variable;
		Token name;               // Variable and Init
		bool byReference = false;
		Ref<AstNode> init;        // Init
	};

	/// A lambda expression: `[captures](params) mutable -> Ret { body }`. Its value is a closure object
	/// (HIR: LambdaLowerer).
	struct LambdaNode : public AstNode
	{
	public:
		LambdaNode() = default;

		CaptureDefault GetCaptureDefault() const { return m_CaptureDefault; }
		const Core::Vector<LambdaCapture>& GetCaptures() const { return m_Captures; }
		bool IsMutable() const { return m_Mutable; }
		const Core::Vector<Ref<ParameterNode>>& GetParameters() const { return m_Parameters; }
		uint32 ParameterCount() const { return m_Parameters.Count(); }
		const Ref<AstNode>& GetReturnType() const { return m_ReturnType; } // null if no `-> Ret`
		const Ref<AstNode>& GetBody() const { return m_Body; }

		/// A lambda starts with `[` in prefix position: there it cannot be an index.
		static Ref<LambdaNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;
	protected:
		NodeType Type() const override { return NodeType::LambdaExpression; }

	private:
		bool ParseCaptureList(ParserState& state);
		bool ParseCapture(ParserState& state);
		bool ParseParameters(ParserState& state);

		CaptureDefault m_CaptureDefault = CaptureDefault::None;
		Core::Vector<LambdaCapture> m_Captures;
		bool m_Mutable = false;
		Core::Vector<Ref<ParameterNode>> m_Parameters;
		Ref<AstNode> m_ReturnType; // QualTypeNode, or null
		Ref<AstNode> m_Body;       // CodeBlockNode
		Core::Vector<Ref<AstNode>> m_Children; // init-capture expressions (the body is not a child: it is its own scope)
	};
}
