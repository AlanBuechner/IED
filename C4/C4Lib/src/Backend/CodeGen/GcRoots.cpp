#include "Backend/CodeGen/GcRoots.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>

namespace Scripting
{
	namespace
	{
		uint64_t SizeOf(ModuleContext& mctx, llvm::GlobalVariable* storage)
		{
			return mctx.module().getDataLayout().getTypeAllocSize(storage->getValueType());
		}

		llvm::Constant* RootRange(ModuleContext& mctx, llvm::StructType* rangeTy, llvm::GlobalVariable* storage)
		{
			return llvm::ConstantStruct::get(rangeTy, { storage, llvm::ConstantInt::get(llvm::Type::getInt64Ty(mctx.context()), SizeOf(mctx, storage)) });
		}

		// Every global, static member and static local whose type may hold a managed pointer, but a script's host's:
		// those are the host's roots.
		std::vector<llvm::GlobalVariable*> RootStorage(ModuleContext& mctx, const ProgramModel& model, const std::unordered_set<std::string>* hostSymbols)
		{
			auto hosts = [&](const llvm::GlobalVariable* g) { return hostSymbols && hostSymbols->count(g->getName().str()); };
			std::vector<llvm::GlobalVariable*> storage;
			model.Symbols()->CollectGlobalVariables([&](const SymbolInfo& info)
				{
					if (info.kind != SymbolKind::Variable || !info.type || !TypeHandle::MayHoldPointers(info.type)) return;
					llvm::GlobalVariable* g = mctx.GlobalAddress(info);
					if (!hosts(g)) storage.push_back(g);
				});
			for (const ModuleContext::StaticStorage& s : mctx.StaticStorages())
				if (TypeHandle::MayHoldPointers(s.type) && !hosts(s.storage)) storage.push_back(s.storage);
			return storage;
		}

		void EmitProcessRoots(ModuleContext& mctx, llvm::StructType* rangeTy, const std::vector<llvm::GlobalVariable*>& storage)
		{
			std::vector<llvm::Constant*> ranges;
			for (llvm::GlobalVariable* s : storage)
				if (!s->isThreadLocal()) ranges.push_back(RootRange(mctx, rangeTy, s));
			ranges.push_back(llvm::ConstantAggregateZero::get(rangeTy));
			auto* tableTy = llvm::ArrayType::get(rangeTy, ranges.size());
			new llvm::GlobalVariable(mctx.module(), tableTy, true, llvm::GlobalValue::ExternalLinkage,
				llvm::ConstantArray::get(tableTy, ranges), "__c4_gc_roots");
		}

		// `__c4_gc_thread_roots(out)` writes the calling thread's copy of each thread_local root to out[i]; the
		// collector calls it on a thread when the thread registers (C4Runtime/GC/GCThreads.c).
		void EmitThreadRoots(ModuleContext& mctx, llvm::StructType* rangeTy, const std::vector<llvm::GlobalVariable*>& storage)
		{
			llvm::LLVMContext& ctx = mctx.context();
			auto* i64 = llvm::Type::getInt64Ty(ctx);
			std::vector<llvm::GlobalVariable*> locals;
			for (llvm::GlobalVariable* s : storage)
				if (s->isThreadLocal()) locals.push_back(s);
			new llvm::GlobalVariable(mctx.module(), i64, true, llvm::GlobalValue::ExternalLinkage,
				llvm::ConstantInt::get(i64, locals.size()), "__c4_gc_thread_root_count");
			auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), { llvm::PointerType::get(ctx, 0) }, false);
			auto* fn = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage, "__c4_gc_thread_roots", mctx.module());
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(ctx, "entry", fn));
			for (size_t i = 0; i < locals.size(); ++i)
			{
				llvm::Value* range = B.CreateConstInBoundsGEP1_64(rangeTy, fn->getArg(0), i);
				B.CreateStore(B.CreateThreadLocalAddress(locals[i]), B.CreateStructGEP(rangeTy, range, 0));
				B.CreateStore(llvm::ConstantInt::get(i64, SizeOf(mctx, locals[i])), B.CreateStructGEP(rangeTy, range, 1));
			}
			B.CreateRetVoid();
		}
	}

	void EmitGcRoots(ModuleContext& mctx, const ProgramModel& model, const std::unordered_set<std::string>* hostSymbols)
	{
		llvm::LLVMContext& ctx = mctx.context();
		auto* rangeTy = llvm::StructType::get(ctx, { llvm::PointerType::get(ctx, 0), llvm::Type::getInt64Ty(ctx) });
		const std::vector<llvm::GlobalVariable*> storage = RootStorage(mctx, model, hostSymbols);
		EmitProcessRoots(mctx, rangeTy, storage);
		// A script's thread_locals are emulated: the runtime makes each thread's object a root (ScriptTlsC.c).
		if (!hostSymbols) EmitThreadRoots(mctx, rangeTy, storage);
	}
}
