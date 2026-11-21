#include "Backend/CodeGen/FuncletLowering.h"
#include "Backend/CodeGen/EhTables.h"
#include "Backend/CodeGen/MirEmitter.h"
#include "Backend/CodeGen/StaticStorage.h"
#include <llvm/IR/Constants.h>

namespace Scripting
{
	namespace
	{
		constexpr uint32_t c_CatchAllAdjectives = 64; // catchpad [null, 64, null] is `catch (...)`
	}

	FuncletLowering::FuncletLowering(MirEmitter& emitter) : m_Emitter(emitter)
	{
		const MirBody& mir = emitter.Mir();
		m_Made.assign(mir.steps.size(), nullptr);
		m_Switches.assign(mir.steps.size(), nullptr);
		m_Pads.assign(mir.funclets.size(), nullptr);
	}

	llvm::BasicBlock* FuncletLowering::UnwindBlock(MirStepId step)
	{
		const MirBody& mir = m_Emitter.Mir();
		// An exception raised in a handler unwinds where its try's catchswitch does: a handler's end is no step here.
		while (step != c_MirNone && mir.steps[step].kind == MirStep::Kind::EndCatch) step = mir.steps[step].next;
		if (step == c_MirNone) return nullptr;
		switch (mir.steps[step].kind)
		{
		case MirStep::Kind::Cleanup: return m_Emitter.BlockOf(mir.steps[step].body);
		case MirStep::Kind::Abandon: return Abandon(step);
		case MirStep::Kind::Dispatch: return Dispatch(step);
		case MirStep::Kind::EndCatch: return nullptr;
		}
		return nullptr;
	}

	llvm::FuncletPadInst* FuncletLowering::Pad(MirFuncletId funclet)
	{
		return funclet != c_MirNone ? m_Pads[funclet] : nullptr;
	}

	llvm::Value* FuncletLowering::ParentPad(MirFuncletId funclet)
	{
		if (llvm::FuncletPadInst* pad = Pad(funclet)) return pad;
		return llvm::ConstantTokenNone::get(m_Emitter.Context().mctx().context());
	}

	// A try is a catchswitch its body's calls unwind to; each clause is a catchpad funclet left with catchret. The frame
	// handler matches the thrown object against each clause's TypeDescriptor in order; what no clause takes continues
	// to whatever the try itself unwinds to.
	llvm::BasicBlock* FuncletLowering::Dispatch(MirStepId step)
	{
		if (m_Made[step]) return m_Made[step];
		const MirBody& mir = m_Emitter.Mir();
		const MirStep& s = mir.steps[step];
		llvm::BasicBlock* block = m_Emitter.NewBlock("catch.dispatch");
		m_Made[step] = block;
		llvm::BasicBlock* unwindTo = UnwindBlock(s.next);
		llvm::IRBuilder<>& B = m_Emitter.Context().builder();
		InsertGuard guard(B, block);
		llvm::CatchSwitchInst* dispatch = B.CreateCatchSwitch(ParentPad(s.parent), unwindTo, s.handlerCount, "catchswitch");
		for (uint32 i = 0; i < s.handlerCount; ++i) dispatch->addHandler(m_Emitter.BlockOf(mir.handlers[s.firstHandler + i].entry));
		m_Switches[step] = dispatch;
		return block;
	}

	// An exception leaving a once region gives up its guard, so the next pass runs the region again.
	llvm::BasicBlock* FuncletLowering::Abandon(MirStepId step)
	{
		if (m_Made[step]) return m_Made[step];
		const MirStep& s = m_Emitter.Mir().steps[step];
		llvm::BasicBlock* block = m_Emitter.NewBlock("ehcleanup");
		m_Made[step] = block;
		llvm::BasicBlock* unwindTo = UnwindBlock(s.next);
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		InsertGuard guard(B, block);
		llvm::CleanupPadInst* pad = B.CreateCleanupPad(ParentPad(s.parent), {}, "cleanuppad");
		ModuleContext& m = ctx.mctx();
		llvm::Function* abandon = s.once->perThread ? StaticStorage::ThreadOnceAbandon(m) : StaticStorage::OnceAbandon(m);
		B.CreateCall(abandon, { m_Emitter.OnceGuard(s.once) }, { llvm::OperandBundleDef("funclet", std::vector<llvm::Value*>{ pad }) });
		B.CreateCleanupRet(pad, unwindTo);
		return block;
	}

	// The frame handler copies the caught value (or, by reference, stores its address) into the clause's slot; `catch
	// (...)` and a noexcept function's catch-all have none.
	void FuncletLowering::BeginHandler(uint32 handler)
	{
		const MirBody& mir = m_Emitter.Mir();
		const MirHandler& entry = mir.handlers[handler];
		Dispatch(entry.dispatch);
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		auto* ptr = llvm::PointerType::get(ctx.mctx().context(), 0);
		llvm::Value* null = llvm::ConstantPointerNull::get(ptr);
		llvm::CatchPadInst* pad = nullptr;
		if (!entry.clause) pad = B.CreateCatchPad(m_Switches[entry.dispatch], { null, B.getInt32(c_CatchAllAdjectives), null });
		else
		{
			const TryNode::CatchClause& clause = *entry.clause;
			const EhTables::Handler table = ctx.mctx().eh().HandlerFor(clause);
			llvm::Value* caught = clause.catchAll ? null : ctx.SlotAlloca(clause.slot);
			pad = B.CreateCatchPad(m_Switches[entry.dispatch], { table.typeDescriptor, B.getInt32(table.adjectives), caught }, "catchpad");
		}
		m_Pads[entry.funclet] = pad;
	}

	// The exception's copy of a cleanup runs in a cleanuppad of its own, then unwinds to what is below it.
	void FuncletLowering::BeginCleanup(MirStepId step)
	{
		const MirStep& s = m_Emitter.Mir().steps[step];
		m_Pads[s.funclet] = m_Emitter.Context().builder().CreateCleanupPad(ParentPad(s.parent), {}, "cleanuppad");
	}

	void FuncletLowering::LeaveHandler(uint32 handler, llvm::BasicBlock* target)
	{
		auto* pad = llvm::cast<llvm::CatchPadInst>(Pad(m_Emitter.Mir().handlers[handler].funclet));
		m_Emitter.Context().builder().CreateCatchRet(pad, target);
	}

	void FuncletLowering::Unwind(MirFuncletId funclet, MirStepId next)
	{
		auto* pad = llvm::cast<llvm::CleanupPadInst>(Pad(funclet));
		llvm::BasicBlock* unwindTo = UnwindBlock(next);
		m_Emitter.Context().builder().CreateCleanupRet(pad, unwindTo);
	}

	void FuncletLowering::Throw(const ThrowNode* node, uint32 handler)
	{
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		ModuleContext& mc = ctx.mctx();
		llvm::Function* raise = mc.GetCxxThrowException();
		if (node->isRethrow)
		{
			// Bare `throw;` is clang's funclet rethrow: _CxxThrowException(null, null) inside the active catchpad re-raises the
			// in-flight exception (value and type) unchanged.
			if (handler != c_MirNone)
			{
				llvm::Constant* null = llvm::ConstantPointerNull::get(llvm::PointerType::get(mc.context(), 0));
				ctx.Call(raise, { null, null }, true);
			}
			B.CreateUnreachable();
			return;
		}
		if (const Type* thrown = node->value ? node->value->type : nullptr)
		{
			llvm::Value* value = m_Emitter.Expr().Emit(node->value);
			llvm::Value* address = ctx.EnsureThrowSlot(thrown);
			B.CreateStore(value, address);
			ctx.Call(raise, { address, mc.eh().ThrowInfo(thrown, { node->destroy, node->copies }) }, true);
		}
		B.CreateUnreachable();
	}

	void FuncletLowering::Terminate()
	{
		IRContext& ctx = m_Emitter.Context();
		llvm::IRBuilder<>& B = ctx.builder();
		llvm::Function* violation = ctx.mctx().RuntimeFunction("c4_noexcept_violation", B.getVoidTy(), {});
		violation->setDoesNotReturn();
		B.CreateCall(violation, {}, { llvm::OperandBundleDef("funclet", std::vector<llvm::Value*>{ m_Emitter.CurrentFunclet() }) });
		B.CreateUnreachable();
	}
} // namespace Scripting
