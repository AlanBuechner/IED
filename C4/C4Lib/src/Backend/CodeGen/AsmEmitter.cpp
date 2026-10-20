#include "Backend/CodeGen/AsmEmitter.h"
#include "Backend/CodeGen/ExprEmitter.h"
#include <llvm/IR/InlineAsm.h>

namespace Scripting
{
	bool AsmEmitter::Emit(const AsmNode* n)
	{
		auto& B = m_Ctx.builder();
		std::vector<llvm::Value*> args;
		std::vector<llvm::Type*> argTypes;
		for (const HIRNode* input : n->inputs)
		{
			args.push_back(m_Expr.Emit(input));
			argTypes.push_back(args.back()->getType());
		}
		auto* type = llvm::FunctionType::get(ResultType(n), argTypes, false);
		auto* assembly = llvm::InlineAsm::get(type, n->text, n->constraints, n->sideEffects, n->alignStack,
			n->intel ? llvm::InlineAsm::AD_Intel : llvm::InlineAsm::AD_ATT);
		llvm::CallInst* call = B.CreateCall(type, assembly, args);
		call->addFnAttr(llvm::Attribute::NoUnwind);
		if (n->memory == AsmNode::Memory::None) call->setMemoryEffects(llvm::MemoryEffects::none());
		else if (n->memory == AsmNode::Memory::Read) call->setMemoryEffects(llvm::MemoryEffects::readOnly());
		for (size_t i = 0; i < n->outputs.size(); ++i)
		{
			llvm::Value* value = n->outputs.size() == 1 ? static_cast<llvm::Value*>(call) : B.CreateExtractValue(call, static_cast<unsigned>(i));
			B.CreateStore(value, m_Expr.LValueAddress(n->outputs[i]));
		}
		if (!n->noReturn) return true;
		B.CreateUnreachable();
		return false;
	}

	llvm::Type* AsmEmitter::ResultType(const AsmNode* n)
	{
		ModuleContext& m = m_Ctx.mctx();
		if (n->outputs.empty()) return llvm::Type::getVoidTy(m.context());
		std::vector<llvm::Type*> types;
		for (const LValue* output : n->outputs) types.push_back(m.Lower(output->type));
		return types.size() == 1 ? types[0] : llvm::StructType::get(m.context(), types);
	}
} // namespace Scripting
