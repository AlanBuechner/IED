#include "Backend/CodeGen/FunctionEmitter.h"
#include "Backend/CodeGen/DebugInfo.h"
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

namespace Scripting
{
	// The MSVC frame handler is a definition's personality before any EH pad exists (LLVM requires that order); a
	// function without exceptions ignores it. A declaration has none. Every definition's unwind table is ApplyTarget's.
	void FunctionEmitter::PrepareDefinition(llvm::Function& fn)
	{
		if (!fn.hasPersonalityFn()) fn.setPersonalityFn(m_MCtx.GetFrameHandler3());
	}

	void FunctionEmitter::Emit(const HIRFunction& hir)
	{
		if (!hir.record || !hir.root || !hir.complete) return; // declarations and incomplete trees emit nothing
		EmitBody(hir);
		// Nested functions declared inside this body are siblings: emit each with its own context.
		for (const auto& nested : hir.nestedFunctions)
			Emit(*nested);
	}

	void FunctionEmitter::EmitImport(const HIRFunction& hir)
	{
		if (!hir.record || !hir.root || !hir.complete) return;
		m_MCtx.SetImporting(true);
		EmitBody(hir);
		m_MCtx.SetImporting(false);
		llvm::Function* fn = m_MCtx.GetOrCreateFunction(*hir.record);
		fn->setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
		fn->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
	}

	// MSVC x64 passes a by-value aggregate wider than a register as a pointer to the caller's own temporary, which the
	// callee owns (it destroys the object; the caller never reads it again): the parameter's slot is that memory, with
	// no copy. A volatile parameter keeps a copy of its own.
	std::vector<llvm::Value*> FunctionEmitter::HeldParameters(llvm::Function& fn, const HIRFunction& hir, unsigned firstParam)
	{
		std::vector<llvm::Value*> held;
		const FunctionType* ft = hir.record->type;
		for (uint32 i = 0; ft && i < ft->GetParameterCount() && firstParam + i < fn.arg_size(); ++i)
		{
			held.push_back(nullptr);
			if (!m_MCtx.abi().PassesByHiddenPtr(ft->GetParameters()[i].m_Type) || hir.GetSlot(i).isVolatile) continue;
			llvm::Argument* argument = fn.getArg(firstParam + i);
			argument->addAttr(llvm::Attribute::NoAlias);
			argument->addAttr(llvm::Attribute::DeadOnReturn);
			held.back() = argument;
		}
		return held;
	}

	void FunctionEmitter::EmitBody(const HIRFunction& hir)
	{
		llvm::Function* fn = m_MCtx.GetOrCreateFunction(*hir.record);
		PrepareDefinition(*fn);
		Abi& abi = m_MCtx.abi();
		const FunctionType* ft = hir.record->type;
		const Abi::Signature sig = m_MCtx.SignatureOf(*hir.record);

		// Entry block. Function::Create leaves the function EMPTY in this LLVM (getEntryBlock()==front()
		// would fault on it), so create the entry block if there is none - the one place every body starts.
		llvm::BasicBlock* entry = fn->empty() ? llvm::BasicBlock::Create(m_MCtx.context(), "entry", fn) : &fn->getEntryBlock();
		IRContext ctx(m_MCtx, hir);
		ctx.StartAt(*entry);
		DebugInfo* debug = m_MCtx.Debug();
		llvm::DISubprogram* described = debug ? debug->Describe(*fn, *hir.record) : nullptr;
		ctx.SetDebugScope(described);
		ctx.At(hir.root->origin); // the prologue is the opening brace
		ctx.CreateSlotAllocas(HeldParameters(*fn, hir, sig.firstParam));

		// The hidden parameters (this, the sret pointer) have no HIR slot; the declared parameters follow them.
		if (sig.thisIndex >= 0) ctx.SetThis(fn->getArg(sig.thisIndex));
		if (sig.sretIndex >= 0) ctx.SetSret(fn->getArg(sig.sretIndex));
		const unsigned llvmArg = sig.firstParam;
		uint32 paramCount = ft ? ft->GetParameterCount() : 0;
		for (uint32 i = 0; i < paramCount && llvmArg + i < fn->arg_size(); ++i)
		{
			// Slot i is the i-th parameter (params occupy slots 0..paramCount-1 in HIR order). A by-value
			// aggregate that rides in one register arrives as an integer of that width - reinterpret its
			// bytes as the object (our struct layout matches clang's, so the byte pattern is the object).
			llvm::Value* p = fn->getArg(llvmArg + i);
			const Type* ptype = ft->GetParameters()[i].m_Type;
			uint32_t intSize = abi.ByValueIntSize(ptype);
			if (intSize != 0)
			{
				auto& B = ctx.builder();
				llvm::Value* intAlloca = B.CreateAlloca(llvm::IntegerType::get(m_MCtx.context(), intSize * 8));
				B.CreateStore(p, intAlloca);
				llvm::Value* asObjPtr = B.CreateBitCast(intAlloca, llvm::PointerType::get(m_MCtx.context(), 0));
				llvm::Value* obj = B.CreateLoad(m_MCtx.Lower(ptype), asObjPtr);
				B.CreateStore(obj, ctx.SlotAlloca(i));
			}
			else if (ctx.SlotAlloca(i) == p)
				continue; // held in the caller's temporary (HeldParameters)
			else if (abi.PassesByHiddenPtr(ptype))
			{
				// A volatile one: p is &temp - LOAD the object through it into its own slot.
				auto& B = ctx.builder();
				llvm::Value* obj = B.CreateLoad(m_MCtx.Lower(ptype), p);
				B.CreateStore(obj, ctx.SlotAlloca(i));
			}
			else
				ctx.builder().CreateStore(p, ctx.SlotAlloca(i));
		}

		if (described) debug->DeclareVariables(described, hir, ctx, *entry);

		// Run the region walker over the root block. The insert point starts at the entry block.
		RegionEmitter regions(ctx);
		ctx.SetInsertPoint(entry);
		if (hir.record->isNoexcept) regions.EmitNoexceptBody(hir.root);
		else regions.EmitBlock(hir.root);

		// Terminate any block left without an end-of-region terminator. Control-flow constructs (if/while/for/switch)
		// create a join/exit block and continue the body there; when such a construct is the LAST statement, that block
		// is left empty. Reaching the end of a void body returns; `main` returns 0 there; any other function that
		// returns a value cannot get there (D-55: lowering rejects a body that could), so the block is unreachable.
		const bool isVoid = fn->getReturnType()->isVoidTy();
		const bool returnsZero = hir.record && IsEntryPoint(*hir.record, "main");
		for (llvm::BasicBlock& bb : *fn)
			if (bb.empty() || !bb.back().isTerminator())
			{
				ctx.builder().SetInsertPoint(&bb);
				if (isVoid) ctx.builder().CreateRetVoid();
				else if (returnsZero) ctx.builder().CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
				else ctx.builder().CreateUnreachable();
			}
		ctx.ZeroUndeclaredSlots();
		// The code after a return or a throw that no label made live (RegionEmitter::ContinueAfterTerminator).
		llvm::EliminateUnreachableBlocks(*fn);
	}
} // namespace Scripting
