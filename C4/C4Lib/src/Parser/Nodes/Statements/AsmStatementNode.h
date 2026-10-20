#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// One operand of an `asm` block (docs/language/16): `in(reg) x;`, `out("eax") low;`, `inout(xmm) v;`,
	/// `in(reg) name = expr;` or `const name = expr;`.
	struct AsmOperand
	{
		enum class Kind { In, Out, InOut, Const };
		Kind kind = Kind::In;
		Core::String registerClass;     // "reg", "xmm", "ymm", "zmm", or a register name when explicitRegister
		bool explicitRegister = false;  // written as a string: in("eax")
		Core::String name;              // what the template calls it
		Ref<AstNode> value;             // the expression (in, const) or the location (out, inout)
		SourceLocation location;
	};

	/// `asm [(options)] { "line"; operands; clobber(...); }` - inline assembly in a body, or (operands not
	/// allowed) assembly added to the program at file scope.
	struct AsmStatementNode : public AstNode
	{
		AsmStatementNode() = default;

		const Core::Vector<Core::String>& GetLines() const { return m_Lines; }
		const Core::Vector<Token>& GetOptions() const { return m_Options; }
		const Core::Vector<AsmOperand>& GetOperands() const { return m_Operands; }
		const Core::Vector<Core::String>& GetClobbers() const { return m_Clobbers; }
		/// `clobber_abi("C")`: every register a C call may change.
		bool ClobbersCallerSaved() const { return m_ClobbersCallerSaved; }

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;
		static Ref<AsmStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::AsmStatement; }

	private:
		void ParseOptions(ParserState& state);
		void ParseItem(ParserState& state);
		void ParseLine(ParserState& state);
		void ParseOperand(ParserState& state, AsmOperand::Kind kind);
		void ParseConstant(ParserState& state);
		void ParseClobbers(ParserState& state, bool abi);
		bool ParseRegisterClass(ParserState& state, AsmOperand& operand);
		bool ExpectSemicolon(ParserState& state, const char* after);

		Core::Vector<Core::String> m_Lines;
		Core::Vector<Token> m_Options;
		Core::Vector<AsmOperand> m_Operands;
		Core::Vector<Core::String> m_Clobbers;
		bool m_ClobbersCallerSaved = false;
		Core::Vector<Ref<AstNode>> m_Children;
	};
}
