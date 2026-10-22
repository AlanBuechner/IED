#include "Backend/CodeGen/StaticStorage.h"
#include "Model/SymbolTable.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <string>

namespace Scripting
{
	namespace
	{
		constexpr unsigned kGuardAlign = 4;

		llvm::Function* NoThrowRuntime(ModuleContext& m, const char* name, llvm::Type* ret, std::vector<llvm::Type*> params)
		{
			llvm::Function* fn = m.RuntimeFunction(name, ret, std::move(params));
			fn->setDoesNotThrow();
			return fn;
		}

		llvm::Type* Ptr(ModuleContext& m) { return llvm::PointerType::get(m.context(), 0); }
		llvm::Type* Void(ModuleContext& m) { return llvm::Type::getVoidTy(m.context()); }

		void CallDestroy(llvm::IRBuilder<>& B, llvm::Function* destroy, llvm::Value* object)
		{
			llvm::CallInst* call = B.CreateCall(destroy, { object });
			call->setCallingConv(destroy->getCallingConv());
		}

		// for (i = count; i > 0; ) { --i; destroy(object + i * stride); }
		void DestroyEachLastFirst(llvm::IRBuilder<>& B, llvm::Function* destroy, llvm::Value* object, uint64 count, uint64 stride)
		{
			llvm::LLVMContext& c = B.getContext();
			llvm::Function* fn = B.GetInsertBlock()->getParent();
			llvm::BasicBlock* entry = B.GetInsertBlock();
			llvm::BasicBlock* loop = llvm::BasicBlock::Create(c, "element", fn);
			llvm::BasicBlock* done = llvm::BasicBlock::Create(c, "done", fn);
			B.CreateBr(loop);
			B.SetInsertPoint(loop);
			llvm::PHINode* remaining = B.CreatePHI(B.getInt64Ty(), 2, "remaining");
			remaining->addIncoming(B.getInt64(count), entry);
			llvm::Value* index = B.CreateSub(remaining, B.getInt64(1), "index");
			CallDestroy(B, destroy, B.CreateGEP(B.getInt8Ty(), object, B.CreateMul(index, B.getInt64(stride))));
			remaining->addIncoming(index, B.GetInsertBlock());
			B.CreateCondBr(B.CreateICmpEQ(index, B.getInt64(0)), done, loop);
			B.SetInsertPoint(done);
		}
	}

	llvm::GlobalVariable* StaticStorage::NewOnceGuard(ModuleContext& m, const Core::String& name)
	{
		llvm::Type* i32 = llvm::Type::getInt32Ty(m.context());
		llvm::StructType* type = llvm::StructType::get(m.context(), { i32, i32 });
		if (m.Importing()) return m.DeclaredLocal(m.LocalName("static.once." + std::string(name.c_str())), type, false);
		auto* guard = new llvm::GlobalVariable(m.module(), type, false, llvm::GlobalValue::InternalLinkage,
			llvm::ConstantAggregateZero::get(type), m.LocalName("static.once." + std::string(name.c_str())));
		m.MakeLocal(*guard); // a script host's table names it
		guard->setAlignment(llvm::Align(kGuardAlign));
		return guard;
	}

	llvm::GlobalVariable* StaticStorage::NewThreadOnceGuard(ModuleContext& m, const Core::String& name)
	{
		llvm::Type* i32 = llvm::Type::getInt32Ty(m.context());
		if (m.Importing()) return m.DeclaredLocal(m.LocalName("thread.once." + std::string(name.c_str())), i32, true);
		auto* guard = new llvm::GlobalVariable(m.module(), i32, false, llvm::GlobalValue::InternalLinkage,
			llvm::ConstantInt::get(i32, 0), m.LocalName("thread.once." + std::string(name.c_str())));
		m.MakeLocal(*guard);
		guard->setThreadLocal(true);
		guard->setAlignment(llvm::Align(kGuardAlign));
		return guard;
	}

	llvm::Value* StaticStorage::IsOnceDone(llvm::IRBuilder<>& B, llvm::GlobalVariable* guard)
	{
		llvm::LoadInst* state = B.CreateAlignedLoad(B.getInt32Ty(), guard, llvm::Align(kGuardAlign), "once.state");
		state->setAtomic(llvm::AtomicOrdering::Acquire);
		return B.CreateICmpEQ(state, B.getInt32(kOnceDone));
	}

	llvm::Function* StaticStorage::OnceBegin(ModuleContext& m)
	{
		return NoThrowRuntime(m, "c4_once_begin", llvm::Type::getInt32Ty(m.context()), { Ptr(m) });
	}

	llvm::Function* StaticStorage::OnceEnd(ModuleContext& m)
	{
		return NoThrowRuntime(m, "c4_once_end", Void(m), { Ptr(m) });
	}

	llvm::Function* StaticStorage::OnceAbandon(ModuleContext& m)
	{
		return NoThrowRuntime(m, "c4_once_abandon", Void(m), { Ptr(m) });
	}

	llvm::Function* StaticStorage::AtExit(ModuleContext& m)
	{
		return NoThrowRuntime(m, "c4_at_exit", Void(m), { Ptr(m), Ptr(m) });
	}

	llvm::Function* StaticStorage::ThreadAtExit(ModuleContext& m)
	{
		return NoThrowRuntime(m, "c4_thread_at_exit", Void(m), { Ptr(m), Ptr(m) });
	}

	llvm::Function* StaticStorage::ThreadOnceAbandon(ModuleContext& m)
	{
		return NoThrowRuntime(m, "c4_thread_once_abandon", Void(m), { Ptr(m) });
	}

	llvm::Function* StaticStorage::ThreadOnceReentered(ModuleContext& m)
	{
		llvm::Function* fn = NoThrowRuntime(m, "c4_thread_once_reentered", Void(m), {});
		fn->setDoesNotReturn();
		return fn;
	}

	llvm::Function* StaticStorage::ExitDestroyer(ModuleContext& m, const FunctionRecord& destroy, uint64 count, uint64 stride)
	{
		const std::string name = "__c4_exit_" + std::string(destroy.mangledName.c_str()) + "_" + std::to_string(count);
		if (llvm::Function* existing = m.module().getFunction(name)) return existing;
		llvm::Function* body = m.GetOrCreateFunction(destroy);
		auto* fn = llvm::Function::Create(llvm::FunctionType::get(Void(m), { Ptr(m) }, false),
			llvm::Function::InternalLinkage, name, m.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(m.context(), "entry", fn));
		if (count == 1) CallDestroy(B, body, fn->getArg(0));
		else DestroyEachLastFirst(B, body, fn->getArg(0), count, stride);
		B.CreateRetVoid();
		return fn;
	}
} // namespace Scripting
