#include "Backend/CodeGen/LandingPadLowering.h"
#include "Backend/CodeGen/EhTables.h"
#include "Backend/CodeGen/MirEmitter.h"
#include "Backend/CodeGen/StaticStorage.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Intrinsics.h>

namespace C4Compiler
{
	LandingPadLowering::LandingPadLowering(MirEmitter& emitter) : m_Emitter(emitter)
	{
		m_LandingPads.assign(emitter.Mir().steps.size(), nullptr);
		m_Steps.assign(emitter.Mir().steps.size(), nullptr);
	}

	// A runtime function of exception handling (C4Runtime/EH); one that raises an exception never returns and unwinds.
	llvm::Function* LandingPadLowering::EhRuntime(const char* name, llvm::Type* result, std::vector<llvm::Type*> params, bool raises)
	{
		llvm::Function* fn = m_Emitter.Context().mctx().RuntimeFunction(name, result, std::move(params));
		if (!raises) return fn;
		fn->setDoesNotReturn();
		fn->removeFnAttr(llvm::Attribute::NoUnwind);
		return fn;
	}

	void LandingPadLowering::EndCatch()
	{
		IRContext& ctx = m_Emitter.Context();
		ctx.Call(EhRuntime("c4_eh_end_catch", ctx.builder().getVoidTy(), {}), {}, false);
	}

	llvm::Value* LandingPadLowering::ExceptionSlot()
	{
		IRContext& ctx = m_Emitter.Context();
		if (!m_ExceptionSlot) m_ExceptionSlot = ctx.EntryAlloca(llvm::PointerType::get(ctx.mctx().context(), 0), "exn.slot");
		return m_ExceptionSlot;
	}

	llvm::Value* LandingPadLowering::SelectorSlot()
	{
		IRContext& ctx = m_Emitter.Context();
		if (!m_SelectorSlot) m_SelectorSlot = ctx.EntryAlloca(llvm::Type::getInt32Ty(ctx.mctx().context()), "sel.slot");
		return m_SelectorSlot;
	}

	llvm::Constant* LandingPadLowering::CaughtType(const MirHandler& handler)
	{
		if (!handler.clause || handler.clause->catchAll) return nullptr;
		return m_Emitter.Context().mctx().eh().HandlerFor(*handler.clause).typeDescriptor;
	}

	llvm::BasicBlock* LandingPadLowering::UnwindBlock(MirStepId step)
	{
		if (step == c_MirNone) return nullptr;
		if (m_LandingPads[step]) return m_LandingPads[step];
		const MirBody& mir = m_Emitter.Mir();
		IRContext& ctx = m_Emitter.Context();
		llvm::LLVMContext& context = ctx.mctx().context();
		auto* ptr = llvm::PointerType::get(context, 0);
		llvm::BasicBlock* block = m_Emitter.NewBlock("lpad");
		m_LandingPads[step] = block;
		llvm::IRBuilder<>& B = ctx.builder();
		InsertGuard guard(B, block);
		llvm::LandingPadInst* pad = B.CreateLandingPad(llvm::StructType::get(context, { ptr, B.getInt32Ty() }), 0, "lpad");
		// The clauses of every dispatch on the way out, innermost first, up to a catch-all; any other step on the way makes
		// the personality land here even when no clause matches.
		bool cleanup = false;
		bool caughtAll = false;
		for (MirStepId s = step; s != c_MirNone && !caughtAll; s = mir.steps[s].next)
		{
			const MirStep& on = mir.steps[s];
			if (on.kind != MirStep::Kind::Dispatch)
			{
				cleanup = true;
				continue;
			}
			for (uint32 i = 0; i < on.handlerCount && !caughtAll; ++i)
			{
				llvm::Constant* caught = CaughtType(mir.handlers[on.firstHandler + i]);
				pad->addClause(caught ? caught : llvm::ConstantPointerNull::get(ptr));
				caughtAll = !caught;
			}
		}
		pad->setCleanup(cleanup);
		B.CreateStore(B.CreateExtractValue(pad, { 0 }), ExceptionSlot());
		B.CreateStore(B.CreateExtractValue(pad, { 1 }), SelectorSlot());
		B.CreateBr(StepBlock(step));
		return block;
	}

	llvm::BasicBlock* LandingPadLowering::StepFrom(MirStepId step)
	{
		return step != c_MirNone ? StepBlock(step) : ResumeBlock();
	}

	// Step `step`'s part of the way out, made once: a cleanup's copy (its end goes on, Unwind), a once region's abandon,
	// a handler's end, a dispatch.
	llvm::BasicBlock* LandingPadLowering::StepBlock(MirStepId step)
	{
		if (m_Steps[step]) return m_Steps[step];
		const MirStep& s = m_Emitter.Mir().steps[step];
		if (s.kind == MirStep::Kind::Cleanup)
		{
			llvm::BasicBlock* copy = m_Emitter.BlockOf(s.body);
			m_Steps[step] = copy;
			return copy;
		}
		llvm::BasicBlock* block = m_Emitter.NewBlock(s.kind == MirStep::Kind::Dispatch ? "catch.dispatch" : "ehcleanup");
		m_Steps[step] = block;
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		InsertGuard guard(B, block);
		switch (s.kind)
		{
		case MirStep::Kind::Dispatch: EmitDispatch(s); return block;
		case MirStep::Kind::EndCatch: EndCatch(); break; // an exception leaving a handler ends its catch
		case MirStep::Kind::Abandon:
		{
			ModuleContext& m = ctx.mctx();
			llvm::Function* abandon = s.once->perThread ? StaticStorage::ThreadOnceAbandon(m) : StaticStorage::OnceAbandon(m);
			ctx.Call(abandon, { m_Emitter.OnceGuard(s.once) }, false);
			break;
		}
		case MirStep::Kind::Cleanup: break;
		}
		B.CreateBr(StepFrom(s.next));
		return block;
	}

	// The first clause whose type the selector names takes the exception (its TypeDescriptor's index in the function's
	// type table, which llvm.eh.typeid.for gives); a catch-all takes any. None: on to the step below.
	void LandingPadLowering::EmitDispatch(const MirStep& dispatch)
	{
		const MirBody& mir = m_Emitter.Mir();
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		llvm::Module& module = ctx.mctx().module();
		auto* ptr = llvm::PointerType::get(module.getContext(), 0);
		llvm::Value* selector = B.CreateLoad(B.getInt32Ty(), SelectorSlot(), "sel");
		for (uint32 i = 0; i < dispatch.handlerCount; ++i)
		{
			const MirHandler& handler = mir.handlers[dispatch.firstHandler + i];
			llvm::BasicBlock* entry = m_Emitter.BlockOf(handler.entry);
			llvm::Constant* caught = CaughtType(handler);
			if (!caught)
			{
				B.CreateBr(entry);
				return;
			}
			llvm::Function* typeId = llvm::Intrinsic::getOrInsertDeclaration(&module, llvm::Intrinsic::eh_typeid_for, { ptr });
			llvm::Value* id = B.CreateCall(typeId, { caught });
			llvm::BasicBlock* next = m_Emitter.NewBlock("catch.next");
			B.CreateCondBr(B.CreateICmpEQ(selector, id), entry, next);
			B.SetInsertPoint(next);
		}
		B.CreateBr(StepFrom(dispatch.next));
	}

	llvm::BasicBlock* LandingPadLowering::ResumeBlock()
	{
		if (m_Resume) return m_Resume;
		m_Resume = m_Emitter.NewBlock("eh.resume");
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		InsertGuard guard(B, m_Resume);
		llvm::LLVMContext& context = ctx.mctx().context();
		auto* ptr = llvm::PointerType::get(context, 0);
		llvm::Value* in = llvm::PoisonValue::get(llvm::StructType::get(context, { ptr, B.getInt32Ty() }));
		in = B.CreateInsertValue(in, B.CreateLoad(ptr, ExceptionSlot(), "exn"), { 0 });
		in = B.CreateInsertValue(in, B.CreateLoad(B.getInt32Ty(), SelectorSlot(), "sel"), { 1 });
		B.CreateResume(in);
		return m_Resume;
	}

	// A handler begins with c4_eh_begin_catch, which makes the copy the frame handler makes on Windows (by reference, a
	// simple type's bits with a pointer adjusted, an object by its copy function). A noexcept function's has nothing to
	// take: it stops the program.
	void LandingPadLowering::BeginHandler(uint32 handler)
	{
		const MirHandler& entry = m_Emitter.Mir().handlers[handler];
		if (!entry.clause) return;
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		auto* ptr = llvm::PointerType::get(ctx.mctx().context(), 0);
		const TryNode::CatchClause& clause = *entry.clause;
		const EhTables::Handler table = ctx.mctx().eh().HandlerFor(clause);
		llvm::Value* caught = clause.catchAll ? llvm::ConstantPointerNull::get(ptr) : ctx.SlotAlloca(clause.slot);
		ctx.Call(EhRuntime("c4_eh_begin_catch", B.getVoidTy(), { ptr, ptr, B.getInt32Ty(), ptr }),
			{ B.CreateLoad(ptr, ExceptionSlot(), "exn"), table.typeDescriptor, B.getInt32(table.adjectives), caught }, false);
	}

	void LandingPadLowering::LeaveHandler(uint32, llvm::BasicBlock* target)
	{
		EndCatch();
		m_Emitter.Context().builder().CreateBr(target);
	}

	void LandingPadLowering::Unwind(MirFuncletId, MirStepId next)
	{
		llvm::BasicBlock* below = StepFrom(next);
		m_Emitter.Context().builder().CreateBr(below);
	}

	// `throw v`: the value goes into an exception the runtime allocates (the stack unwinds before a handler runs, so it
	// cannot live in this frame), then c4_eh_throw raises it with its ThrowInfo. `throw;` rethrows the caught exception.
	void LandingPadLowering::Throw(const ThrowNode* node, uint32)
	{
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		ModuleContext& mc = ctx.mctx();
		auto* ptr = llvm::PointerType::get(mc.context(), 0);
		if (node->isRethrow) ctx.Call(EhRuntime("c4_eh_rethrow", B.getVoidTy(), {}, true), {}, true);
		else if (const Type* thrown = node->value ? node->value->type : nullptr)
		{
			llvm::Value* value = m_Emitter.Expr().Emit(node->value);
			const uint64_t size = mc.layout().getTypeAllocSize(mc.Lower(thrown));
			llvm::Value* exception = ctx.Call(EhRuntime("c4_eh_allocate", ptr, { B.getInt64Ty() }), { B.getInt64(size) }, false);
			B.CreateStore(value, exception);
			ctx.Call(EhRuntime("c4_eh_throw", B.getVoidTy(), { ptr, ptr }, true),
				{ exception, mc.eh().ThrowInfo(thrown, { node->destroy, node->copies }) }, true);
		}
		B.CreateUnreachable();
	}

	void LandingPadLowering::Terminate()
	{
		IRContext& ctx = m_Emitter.Context();
		llvm::Function* violation = EhRuntime("c4_noexcept_violation", ctx.builder().getVoidTy(), {});
		violation->setDoesNotReturn();
		ctx.Call(violation, {}, false);
		ctx.builder().CreateUnreachable();
	}
} // namespace C4Compiler
