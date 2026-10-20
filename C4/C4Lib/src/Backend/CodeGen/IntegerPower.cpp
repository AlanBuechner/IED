#include "Backend/CodeGen/IntegerPower.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <string>

namespace Scripting
{
	namespace
	{
		// base ** exponent for exponent < 0: only 1 and -1 have an integer result.
		llvm::Value* NegativeExponent(llvm::IRBuilder<>& B, llvm::Value* base, llvm::Value* exponent)
		{
			llvm::Type* t = base->getType();
			llvm::Value* one = llvm::ConstantInt::get(t, 1);
			llvm::Value* minusOne = llvm::ConstantInt::getSigned(t, -1);
			llvm::Value* odd = B.CreateTrunc(B.CreateAnd(exponent, one), B.getInt1Ty());
			llvm::Value* ofMinusOne = B.CreateSelect(odd, minusOne, one);
			llvm::Value* rest = B.CreateSelect(B.CreateICmpEQ(base, minusOne), ofMinusOne, llvm::ConstantInt::get(t, 0));
			return B.CreateSelect(B.CreateICmpEQ(base, one), one, rest);
		}

		void SquareAndMultiply(llvm::IRBuilder<>& B, llvm::Function* fn, llvm::BasicBlock* entry, llvm::Value* base, llvm::Value* exponent)
		{
			llvm::LLVMContext& ctx = fn->getContext();
			llvm::Type* t = base->getType();
			auto* loop = llvm::BasicBlock::Create(ctx, "loop", fn);
			auto* body = llvm::BasicBlock::Create(ctx, "body", fn);
			auto* done = llvm::BasicBlock::Create(ctx, "done", fn);
			B.CreateBr(loop);

			B.SetInsertPoint(loop);
			llvm::PHINode* result = B.CreatePHI(t, 2, "result");
			llvm::PHINode* square = B.CreatePHI(t, 2, "square");
			llvm::PHINode* rest = B.CreatePHI(t, 2, "rest");
			result->addIncoming(llvm::ConstantInt::get(t, 1), entry);
			square->addIncoming(base, entry);
			rest->addIncoming(exponent, entry);
			B.CreateCondBr(B.CreateICmpEQ(rest, llvm::ConstantInt::get(t, 0)), done, body);

			B.SetInsertPoint(body);
			llvm::Value* bitSet = B.CreateICmpNE(B.CreateAnd(rest, llvm::ConstantInt::get(t, 1)), llvm::ConstantInt::get(t, 0));
			result->addIncoming(B.CreateSelect(bitSet, B.CreateMul(result, square), result), body);
			square->addIncoming(B.CreateMul(square, square), body);
			rest->addIncoming(B.CreateLShr(rest, llvm::ConstantInt::get(t, 1)), body);
			B.CreateBr(loop);

			B.SetInsertPoint(done);
			B.CreateRet(result);
		}
	}

	llvm::Function* IntegerPowerFunction(llvm::Module& module, llvm::IntegerType* type, bool isSigned)
	{
		const std::string name = std::string("c4.ipow.") + (isSigned ? "s" : "u") + std::to_string(type->getBitWidth());
		if (llvm::Function* existing = module.getFunction(name)) return existing;
		auto* fnType = llvm::FunctionType::get(type, { type, type }, false);
		auto* fn = llvm::Function::Create(fnType, llvm::Function::InternalLinkage, name, module);
		llvm::LLVMContext& ctx = module.getContext();
		llvm::Value* base = fn->getArg(0);
		llvm::Value* exponent = fn->getArg(1);

		auto* entry = llvm::BasicBlock::Create(ctx, "entry", fn);
		llvm::IRBuilder<> B(entry);
		if (isSigned)
		{
			auto* negative = llvm::BasicBlock::Create(ctx, "negative", fn);
			auto* counting = llvm::BasicBlock::Create(ctx, "counting", fn);
			B.CreateCondBr(B.CreateICmpSLT(exponent, llvm::ConstantInt::get(type, 0)), negative, counting);
			B.SetInsertPoint(negative);
			B.CreateRet(NegativeExponent(B, base, exponent));
			B.SetInsertPoint(counting);
			SquareAndMultiply(B, fn, counting, base, exponent);
			return fn;
		}
		SquareAndMultiply(B, fn, entry, base, exponent);
		return fn;
	}
}
