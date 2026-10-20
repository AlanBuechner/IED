#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/AsmRegisters.h"
#include <string>
#include <vector>

namespace Scripting
{
	struct AsmStatementNode; struct AsmOperand;

	/// An `asm` statement (docs/language/16) as an AsmNode: its operands evaluated and placed in registers, the
	/// template and constraints in LLVM's terms, then each output register stored into its location.
	class AsmLowerer
	{
	public:
		explicit AsmLowerer(LoweringContext& ctx) : m_Ctx(ctx) {}

		QueryResult<void> Lower(const AsmStatementNode* n, BlockNode* block);

	private:
		struct Options { bool att = false, nomem = false, readonly = false, pure = false, nostack = false, preservesFlags = false, noReturn = false; };
		/// A value as a register holds it: the kind of register and the type of the value in it.
		struct RegisterValue { AsmRegisters::Kind kind; const Type* type; };
		/// LLVM numbers the outputs first, then the inputs; `inputOperand` marks which list each operand's number is in.
		struct Plan
		{
			std::vector<const LValue*> locations;  // where each output goes
			std::vector<const LValue*> slots;      // the register value of each output, before it is stored there
			std::vector<std::string> outputConstraints;
			std::vector<const HIRNode*> inputs;
			std::vector<std::string> inputConstraints;
			std::vector<int> operandIndex;
			std::vector<bool> inputOperand;
		};

		QueryResult<Options> ReadOptions(const AsmStatementNode* n);
		QueryResult<void> LowerOperands(const AsmStatementNode* n, const Options& options, BlockNode* block);
		QueryResult<void> AddOperand(const AsmOperand& op, Plan& plan);
		QueryResult<void> AddOutput(const AsmOperand& op, Plan& plan);
		QueryResult<void> AddInput(const AsmOperand& op, Plan& plan);
		QueryResult<RegisterValue> Classify(const AsmOperand& op, const Type* type);
		QueryResult<std::string> Letter(const AsmOperand& op, const RegisterValue& value, const Type* type);
		QueryResult<const LValue*> Location(const AsmOperand& op);
		const HIRNode* InRegister(const HIRNode* value, const RegisterValue& reg);
		QueryResult<const HIRNode*> Constant(const AsmOperand& op);
		QueryResult<std::string> Clobbers(const AsmStatementNode* n, const Options& options);
		int TemplateIndex(const AsmStatementNode* n, const Plan& plan, const std::string& name) const;
		const LValue* NewSlot(const Type* type);

		LoweringContext& m_Ctx;
	};
} // namespace Scripting
