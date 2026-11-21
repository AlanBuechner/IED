#include "Backend/CodeGen/GcRoots.h"
#include "Backend/CodeGen/PointerMaps.h"
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
		constexpr uint64_t kWordAlignment = 8;

		// A global that may hold a managed pointer and the type it stores.
		struct RootStorage
		{
			llvm::GlobalVariable* global;
			const Type* type;
		};

		uint64_t SizeOf(ModuleContext& mctx, llvm::GlobalVariable* storage)
		{
			return mctx.module().getDataLayout().getTypeAllocSize(storage->getValueType());
		}

		// The storage's pointer map (R16), or null: read whole. A map needs the storage on a word boundary.
		llvm::Constant* MapOf(ModuleContext& mctx, const RootStorage& root)
		{
			const bool onWords = root.global->getPointerAlignment(mctx.module().getDataLayout()).value() >= kWordAlignment;
			const std::optional<PointerMap> map = onWords ? PointerMap::Of(root.type) : std::nullopt;
			return map ? map->Emit(mctx.module()) : llvm::ConstantPointerNull::get(llvm::PointerType::get(mctx.context(), 0));
		}

		llvm::Constant* RootRange(ModuleContext& mctx, llvm::StructType* rangeTy, const RootStorage& root)
		{
			return llvm::ConstantStruct::get(rangeTy, { root.global, llvm::ConstantInt::get(llvm::Type::getInt64Ty(mctx.context()), SizeOf(mctx, root.global)),
				MapOf(mctx, root) });
		}

		// Every global, static member and static local whose type may hold a managed pointer, but a script's host's:
		// those are the host's roots.
		std::vector<RootStorage> RootStorages(ModuleContext& mctx, const ProgramModel& model, const AskedNames* hostSymbols)
		{
			auto hosts = [&](const llvm::GlobalVariable* g) { return hostSymbols && hostSymbols->Has(g->getName().str()); };
			std::vector<RootStorage> storage;
			model.Symbols()->CollectGlobalVariables([&](const SymbolInfo& info)
				{
					if (info.kind != SymbolKind::Variable || !info.type || !TypeHandle::MayHoldPointers(info.type)) return;
					llvm::GlobalVariable* g = mctx.GlobalAddress(info);
					if (!hosts(g)) storage.push_back({ g, info.type });
				});
			for (const ModuleContext::StaticStorage& s : mctx.StaticStorages())
				if (TypeHandle::MayHoldPointers(s.type) && !hosts(s.storage)) storage.push_back({ s.storage, s.type });
			return storage;
		}

		void EmitProcessRoots(ModuleContext& mctx, llvm::StructType* rangeTy, const std::vector<RootStorage>& storage)
		{
			std::vector<llvm::Constant*> ranges;
			for (const RootStorage& s : storage)
				if (!s.global->isThreadLocal()) ranges.push_back(RootRange(mctx, rangeTy, s));
			ranges.push_back(llvm::ConstantAggregateZero::get(rangeTy));
			auto* tableTy = llvm::ArrayType::get(rangeTy, ranges.size());
			new llvm::GlobalVariable(mctx.module(), tableTy, true, llvm::GlobalValue::ExternalLinkage,
				llvm::ConstantArray::get(tableTy, ranges), "__c4_gc_roots");
		}

		// `__c4_gc_thread_roots(out)` writes the calling thread's copy of each thread_local root to out[i]; the
		// collector calls it on a thread when the thread registers (C4Runtime/GC/GCThreads.c).
		void EmitThreadRoots(ModuleContext& mctx, llvm::StructType* rangeTy, const std::vector<RootStorage>& storage)
		{
			llvm::LLVMContext& ctx = mctx.context();
			auto* i64 = llvm::Type::getInt64Ty(ctx);
			std::vector<RootStorage> locals;
			for (const RootStorage& s : storage)
				if (s.global->isThreadLocal()) locals.push_back(s);
			new llvm::GlobalVariable(mctx.module(), i64, true, llvm::GlobalValue::ExternalLinkage,
				llvm::ConstantInt::get(i64, locals.size()), "__c4_gc_thread_root_count");
			auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), { llvm::PointerType::get(ctx, 0) }, false);
			auto* fn = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage, "__c4_gc_thread_roots", mctx.module());
			llvm::IRBuilder<> B(llvm::BasicBlock::Create(ctx, "entry", fn));
			for (size_t i = 0; i < locals.size(); ++i)
			{
				llvm::Value* range = B.CreateConstInBoundsGEP1_64(rangeTy, fn->getArg(0), i);
				B.CreateStore(B.CreateThreadLocalAddress(locals[i].global), B.CreateStructGEP(rangeTy, range, 0));
				B.CreateStore(llvm::ConstantInt::get(i64, SizeOf(mctx, locals[i].global)), B.CreateStructGEP(rangeTy, range, 1));
				B.CreateStore(MapOf(mctx, locals[i]), B.CreateStructGEP(rangeTy, range, 2));
			}
			B.CreateRetVoid();
		}
	}

	void EmitGcRoots(ModuleContext& mctx, const ProgramModel& model, const AskedNames* hostSymbols)
	{
		llvm::LLVMContext& ctx = mctx.context();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		auto* rangeTy = llvm::StructType::get(ctx, { ptr, llvm::Type::getInt64Ty(ctx), ptr });
		const std::vector<RootStorage> storage = RootStorages(mctx, model, hostSymbols);
		EmitProcessRoots(mctx, rangeTy, storage);
		// A script's thread_locals are emulated: the runtime makes each thread's object a root (ScriptTlsC.c).
		if (!hostSymbols) EmitThreadRoots(mctx, rangeTy, storage);
	}
}
