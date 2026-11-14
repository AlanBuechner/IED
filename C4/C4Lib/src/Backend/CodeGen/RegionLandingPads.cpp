// RegionEmitter's landing pads: how exceptions unwind on Linux (D-86, docs/compiler/04 "Scope exit and unwinding").
// Windows lowers the same cleanup stack to funclets (RegionEmitter.cpp); the tables a throw and a catch name are the
// same (EhTables), read by the C4 runtime's personality routine (C4Runtime/EH).
#include "Backend/CodeGen/RegionEmitter.h"
#include "Backend/CodeGen/EhTables.h"
#include <llvm/IR/Intrinsics.h>

namespace Scripting
{
	// A runtime function of exception handling; one that raises an exception never returns and unwinds.
	llvm::Function* RegionEmitter::EhRuntime(const char* name, llvm::Type* result, std::vector<llvm::Type*> params, bool raises)
	{
		llvm::Function* fn = m_Ctx.mctx().RuntimeFunction(name, result, std::move(params));
		if (!raises) return fn;
		fn->setDoesNotReturn();
		fn->removeFnAttr(llvm::Attribute::NoUnwind);
		return fn;
	}

	void RegionEmitter::EndCatch()
	{
		m_Ctx.Call(EhRuntime("c4_eh_end_catch", m_Ctx.builder().getVoidTy(), {}), {}, false);
	}

	llvm::Value* RegionEmitter::ExceptionSlot()
	{
		if (!m_ExceptionSlot) m_ExceptionSlot = m_Ctx.EntryAlloca(llvm::PointerType::get(m_Ctx.mctx().context(), 0), "exn.slot");
		return m_ExceptionSlot;
	}

	llvm::Value* RegionEmitter::SelectorSlot()
	{
		if (!m_SelectorSlot) m_SelectorSlot = m_Ctx.EntryAlloca(llvm::Type::getInt32Ty(m_Ctx.mctx().context()), "sel.slot");
		return m_SelectorSlot;
	}

	void RegionEmitter::EmitLandingPadTry(const TryNode* n)
	{
		auto& B = m_Ctx.builder();
		llvm::BasicBlock* joinBB = NewBlock("try.cont");
		std::vector<llvm::BasicBlock*> handlers;
		CleanupEntry tryEntry;
		tryEntry.kind = CleanupEntry::Kind::Try;
		tryEntry.floor = m_Ctx.Floor();
		for (const TryNode::CatchClause& clause : n->catches)
		{
			handlers.push_back(NewBlock("catch"));
			tryEntry.catches.push_back({ clause.catchAll ? nullptr : m_Ctx.mctx().eh().HandlerFor(clause).typeDescriptor, handlers.back() });
		}
		const size_t tryDepth = m_Ctx.Cleanups().Depth();
		m_Ctx.Cleanups().Push(tryEntry);
		EmitBlock(n->tryBlock);
		if (IsOpen()) B.CreateBr(joinBB);
		m_Ctx.Cleanups().PopTo(tryDepth);
		for (size_t i = 0; i < n->catches.size(); ++i)
			EmitHandler(n->catches[i], nullptr, handlers[i], joinBB);
		B.SetInsertPoint(joinBB);
	}

	void RegionEmitter::EmitLandingPadNoexceptBody(const BlockNode* root)
	{
		auto& B = m_Ctx.builder();
		llvm::BasicBlock* handlerBB = NewBlock("noexcept.terminate");
		{
			InsertGuard g(B, handlerBB);
			llvm::Function* violation = EhRuntime("c4_noexcept_violation", B.getVoidTy(), {});
			violation->setDoesNotReturn();
			m_Ctx.Call(violation, {}, false);
			B.CreateUnreachable();
		}
		CleanupEntry entry;
		entry.kind = CleanupEntry::Kind::Try;
		entry.floor = m_Ctx.Floor();
		entry.catches.push_back({ nullptr, handlerBB });
		const size_t depth = m_Ctx.Cleanups().Depth();
		m_Ctx.Cleanups().Push(entry);
		EmitBlock(root);
		m_Ctx.Cleanups().PopTo(depth);
	}

	// `throw v`: the value goes into an exception the runtime allocates (the stack unwinds before a handler runs, so it
	// cannot live in this frame), then c4_eh_throw raises it with its ThrowInfo. `throw;` rethrows the caught exception.
	void RegionEmitter::EmitLandingPadThrow(const ThrowNode* n)
	{
		auto& B = m_Ctx.builder();
		ModuleContext& mc = m_Ctx.mctx();
		auto* ptr = llvm::PointerType::get(mc.context(), 0);
		if (n->isRethrow) m_Ctx.Call(EhRuntime("c4_eh_rethrow", B.getVoidTy(), {}, true), {}, true);
		else if (const Type* thrown = n->value ? n->value->type : nullptr)
		{
			llvm::Value* value = m_Expr.Emit(n->value);
			const uint64_t size = mc.layout().getTypeAllocSize(mc.Lower(thrown));
			llvm::Value* exception = m_Ctx.Call(EhRuntime("c4_eh_allocate", ptr, { B.getInt64Ty() }), { B.getInt64(size) }, false);
			B.CreateStore(value, exception);
			m_Ctx.Call(EhRuntime("c4_eh_throw", B.getVoidTy(), { ptr, ptr }, true),
				{ exception, mc.eh().ThrowInfo(thrown, { n->destroy, n->copies }) }, true);
		}
		B.CreateUnreachable();
		ContinueAfterTerminator();
	}

	llvm::BasicBlock* RegionEmitter::LandingPadFor(size_t index)
	{
		CleanupStack& cs = m_Ctx.Cleanups();
		if (cs.At(index).landingPad) return cs.At(index).landingPad;
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		llvm::BasicBlock* block = NewBlock("lpad");
		cs.At(index).landingPad = block;
		auto& B = m_Ctx.builder();
		InsertGuard guard(B, block);
		llvm::LandingPadInst* pad = B.CreateLandingPad(llvm::StructType::get(ctx, { ptr, B.getInt32Ty() }), 0, "lpad");
		// The clauses of every try on the way out, innermost first, up to a catch (...); a cleanup or a handler on the
		// way makes the personality land here even when no clause matches.
		bool cleanup = false;
		bool caughtAll = false;
		const size_t floor = cs.At(index).floor;
		for (size_t i = index + 1; i-- > floor && !caughtAll; )
		{
			const CleanupEntry& e = cs.At(i);
			if (e.kind != CleanupEntry::Kind::Try)
			{
				cleanup = true;
				continue;
			}
			for (const auto& [typeDescriptor, handler] : e.catches)
			{
				pad->addClause(typeDescriptor ? typeDescriptor : llvm::ConstantPointerNull::get(ptr));
				if (!typeDescriptor)
				{
					caughtAll = true;
					break;
				}
			}
		}
		pad->setCleanup(cleanup);
		B.CreateStore(B.CreateExtractValue(pad, { 0 }), ExceptionSlot());
		B.CreateStore(B.CreateExtractValue(pad, { 1 }), SelectorSlot());
		B.CreateBr(EhStepOf(index));
		return block;
	}

	// Entry `index`'s step of an exception's way out, emitted once: with only the scopes outside it visible, as a
	// cleanup's exception path is (CleanupPadFor), then on to the entry below.
	llvm::BasicBlock* RegionEmitter::EhStepOf(size_t index)
	{
		CleanupStack& cs = m_Ctx.Cleanups();
		if (cs.At(index).ehPad) return cs.At(index).ehPad;
		const CleanupEntry entry = cs.At(index);
		llvm::BasicBlock* block = NewBlock(entry.kind == CleanupEntry::Kind::Try ? "catch.dispatch" : "ehcleanup");
		cs.At(index).ehPad = block;
		auto& B = m_Ctx.builder();
		InsertGuard guard(B, block);
		if (entry.kind == CleanupEntry::Kind::Try)
		{
			EmitDispatch(entry, index);
			return block;
		}
		if (entry.kind == CleanupEntry::Kind::Handler)
		{
			EndCatch(); // an exception leaving a handler ends its catch
			B.CreateBr(EhStepBelow(index, entry.floor));
			return block;
		}
		std::vector<CleanupEntry> above = cs.DetachFrom(index);
		IRContext::FuncletState saved = m_Ctx.EnterCleanupFunclet(index, nullptr);
		EmitCleanupBody(entry);
		if (IsOpen()) B.CreateBr(EhStepBelow(index, entry.floor));
		m_Ctx.LeaveCleanupFunclet(saved);
		cs.Restore(std::move(above));
		return block;
	}

	llvm::BasicBlock* RegionEmitter::EhStepBelow(size_t index, size_t floor)
	{
		return index > floor ? EhStepOf(index - 1) : ResumeBlock();
	}

	// The first clause whose type the selector names takes the exception (its TypeDescriptor's index in the function's
	// type table, which llvm.eh.typeid.for gives); `catch (...)` takes any. None: on to the entry below.
	void RegionEmitter::EmitDispatch(const CleanupEntry& entry, size_t index)
	{
		auto& B = m_Ctx.builder();
		llvm::Module& module = m_Ctx.mctx().module();
		auto* ptr = llvm::PointerType::get(module.getContext(), 0);
		llvm::Value* selector = B.CreateLoad(B.getInt32Ty(), SelectorSlot(), "sel");
		for (const auto& [typeDescriptor, handler] : entry.catches)
		{
			if (!typeDescriptor)
			{
				B.CreateBr(handler);
				return;
			}
			llvm::Function* typeId = llvm::Intrinsic::getOrInsertDeclaration(&module, llvm::Intrinsic::eh_typeid_for, { ptr });
			llvm::Value* id = B.CreateCall(typeId, { typeDescriptor });
			llvm::BasicBlock* next = NewBlock("catch.next");
			B.CreateCondBr(B.CreateICmpEQ(selector, id), handler, next);
			B.SetInsertPoint(next);
		}
		B.CreateBr(EhStepBelow(index, entry.floor));
	}

	llvm::BasicBlock* RegionEmitter::ResumeBlock()
	{
		if (m_Resume) return m_Resume;
		m_Resume = NewBlock("eh.resume");
		auto& B = m_Ctx.builder();
		InsertGuard guard(B, m_Resume);
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		llvm::Value* in = llvm::PoisonValue::get(llvm::StructType::get(ctx, { ptr, B.getInt32Ty() }));
		in = B.CreateInsertValue(in, B.CreateLoad(ptr, ExceptionSlot(), "exn"), { 0 });
		in = B.CreateInsertValue(in, B.CreateLoad(B.getInt32Ty(), SelectorSlot(), "sel"), { 1 });
		B.CreateResume(in);
		return m_Resume;
	}
} // namespace Scripting
