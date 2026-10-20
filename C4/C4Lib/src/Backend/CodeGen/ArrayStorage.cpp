#include "Backend/CodeGen/ArrayStorage.h"
#include "Model/SymbolTable.h"
#include <llvm/IR/Instructions.h>
#include <string>

namespace Scripting
{
	namespace
	{
		constexpr uint64 kCountSize = sizeof(int64);

		llvm::Type* Ptr(ModuleContext& m) { return llvm::PointerType::get(m.context(), 0); }

		// for (i = count; i > 0; ) { --i; destroy(first + i * stride); } with count > 0
		void DestroyLastFirst(llvm::IRBuilder<>& B, llvm::Function* destroy, llvm::Value* first, llvm::Value* count, uint64 stride)
		{
			llvm::LLVMContext& c = B.getContext();
			llvm::Function* fn = B.GetInsertBlock()->getParent();
			llvm::BasicBlock* entry = B.GetInsertBlock();
			llvm::BasicBlock* loop = llvm::BasicBlock::Create(c, "element", fn);
			llvm::BasicBlock* done = llvm::BasicBlock::Create(c, "done", fn);
			B.CreateCondBr(B.CreateICmpEQ(count, B.getInt64(0)), done, loop);
			B.SetInsertPoint(loop);
			llvm::PHINode* remaining = B.CreatePHI(B.getInt64Ty(), 2, "remaining");
			remaining->addIncoming(count, entry);
			llvm::Value* index = B.CreateSub(remaining, B.getInt64(1), "index");
			llvm::CallInst* call = B.CreateCall(destroy, { B.CreateGEP(B.getInt8Ty(), first, B.CreateMul(index, B.getInt64(stride))) });
			call->setCallingConv(destroy->getCallingConv());
			remaining->addIncoming(index, B.GetInsertBlock());
			B.CreateCondBr(B.CreateICmpEQ(index, B.getInt64(0)), done, loop);
			B.SetInsertPoint(done);
		}
	}

	uint64 ArrayStorage::CountBytes(uint64 elementAlignment)
	{
		const uint64 align = elementAlignment ? elementAlignment : 1;
		return (kCountSize + align - 1) / align * align;
	}

	void ArrayStorage::ForEach(llvm::IRBuilder<>& B, llvm::Value* first, llvm::Value* count, uint64 stride,
		const std::function<void(llvm::Value*)>& visit)
	{
		llvm::LLVMContext& c = B.getContext();
		llvm::Function* fn = B.GetInsertBlock()->getParent();
		llvm::BasicBlock* entry = B.GetInsertBlock();
		llvm::BasicBlock* loop = llvm::BasicBlock::Create(c, "array.element", fn);
		llvm::BasicBlock* done = llvm::BasicBlock::Create(c, "array.done", fn);
		B.CreateCondBr(B.CreateICmpEQ(count, B.getInt64(0)), done, loop);
		B.SetInsertPoint(loop);
		llvm::PHINode* index = B.CreatePHI(B.getInt64Ty(), 2, "array.index");
		index->addIncoming(B.getInt64(0), entry);
		visit(B.CreateGEP(B.getInt8Ty(), first, B.CreateMul(index, B.getInt64(stride))));
		llvm::Value* next = B.CreateAdd(index, B.getInt64(1));
		index->addIncoming(next, B.GetInsertBlock());
		B.CreateCondBr(B.CreateICmpEQ(next, count), done, loop);
		B.SetInsertPoint(done);
	}

	llvm::Function* ArrayStorage::Finalizer(ModuleContext& m, const FunctionRecord& destroy, uint64 perElement, uint64 stride, uint64 countBytes)
	{
		const std::string name = "__c4_array_finalizer_" + std::string(destroy.mangledName.c_str()) + "_" + std::to_string(perElement) + "_" +
			std::to_string(countBytes);
		if (llvm::Function* existing = m.module().getFunction(name)) return existing;
		llvm::Function* body = m.GetOrCreateFunction(destroy);
		auto* fn = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(m.context()), { Ptr(m) }, false),
			llvm::Function::InternalLinkage, name, m.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(m.context(), "entry", fn));
		llvm::Value* base = fn->getArg(0);
		llvm::Value* count = B.CreateMul(B.CreateLoad(B.getInt64Ty(), base, "count"), B.getInt64(perElement));
		DestroyLastFirst(B, body, B.CreateGEP(B.getInt8Ty(), base, B.getInt64(countBytes)), count, stride);
		B.CreateRetVoid();
		return fn;
	}
} // namespace Scripting
