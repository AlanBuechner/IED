#include "Backend/CodeGen/MirEmitter.h"
#include "Backend/CodeGen/AsmEmitter.h"
#include "Backend/CodeGen/FuncletLowering.h"
#include "Backend/CodeGen/LandingPadLowering.h"
#include "Backend/CodeGen/StaticStorage.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>

namespace C4Compiler
{
	std::unique_ptr<EhLowering> EhLowering::For(ExceptionScheme scheme, MirEmitter& emitter)
	{
		if (scheme == ExceptionScheme::Funclets) return std::make_unique<FuncletLowering>(emitter);
		return std::make_unique<LandingPadLowering>(emitter);
	}

	MirEmitter::MirEmitter(IRContext& ctx, const MirBody& mir) : m_Ctx(ctx), m_Expr(ctx), m_Mir(mir)
	{
		m_Eh = EhLowering::For(ctx.mctx().Target().Exceptions(), *this);
		ctx.SetBlockServices(this);
	}

	MirEmitter::~MirEmitter()
	{
		m_Ctx.SetBlockServices(nullptr);
	}

	void MirEmitter::Emit()
	{
		const size_t count = m_Mir.blocks.size();
		if (count == 0) return;
		m_Blocks.assign(count, BlockState());
		for (const MirBlock& block : m_Mir.blocks) m_Mir.ForEachSuccessor(block, [&](MirBlockId next) { ++m_Blocks[next].predecessors; });
		for (const MirRegion& region : m_Mir.regions) m_Blocks[region.entry].apart = true;
		for (const MirHandler& handler : m_Mir.handlers) m_Blocks[handler.entry].apart = true;
		for (MirStepId s = 0; s < m_Mir.steps.size(); ++s)
			if (m_Mir.steps[s].body != c_MirNone)
			{
				m_Blocks[m_Mir.steps[s].body].apart = true;
				m_Blocks[m_Mir.steps[s].body].cleanupOf = s;
			}

		m_Blocks[MirBody::c_Entry].llvm = m_Ctx.builder().GetInsertBlock();
		EmitFrom(MirBody::c_Entry);
		for (size_t i = 0; i < m_Pending.size(); ++i) // in the order they were first needed
		{
			const MirBlockId next = m_Pending[i];
			m_Ctx.SetInsertPoint(m_Blocks[next].llvm);
			EmitFrom(next);
		}
		TerminateOpenBlocks();
	}

	// --- blocks ---------------------------------------------------------------------------------------------------

	llvm::BasicBlock* MirEmitter::NewBlock(const char* name)
	{
		return llvm::BasicBlock::Create(m_Ctx.mctx().context(), name, m_Ctx.builder().GetInsertBlock()->getParent());
	}

	llvm::BasicBlock* MirEmitter::BlockOf(MirBlockId block)
	{
		if (!m_Blocks[block].llvm)
		{
			m_Blocks[block].llvm = NewBlock("bb");
			m_Pending.push_back(block);
		}
		return m_Blocks[block].llvm;
	}

	llvm::BasicBlock* MirEmitter::RegionEmission::Find(MirBlockId block) const
	{
		for (const auto& [id, at] : blocks)
			if (id == block) return at;
		return nullptr;
	}

	llvm::BasicBlock* MirEmitter::TargetOf(MirBlockId block)
	{
		if (m_Regions.empty()) return BlockOf(block);
		RegionEmission& region = *m_Regions.back();
		if (llvm::BasicBlock* made = region.Find(block)) return made;
		llvm::BasicBlock* at = NewBlock("bb");
		region.blocks.push_back({ block, at });
		region.pending.push_back(block);
		return at;
	}

	bool MirEmitter::Fuses(MirBlockId block) const
	{
		if (m_Blocks[block].predecessors != 1 || m_Blocks[block].apart) return false;
		if (m_Regions.empty()) return !m_Blocks[block].llvm;
		return !m_Regions.back()->Find(block);
	}

	void MirEmitter::MarkEmitted(MirBlockId block, llvm::BasicBlock* at)
	{
		if (m_Regions.empty()) m_Blocks[block].llvm = at;
		else m_Regions.back()->blocks.push_back({ block, at });
	}

	void MirEmitter::EmitFrom(MirBlockId block)
	{
		for (MirBlockId next = block; next != c_MirNone; )
		{
			if (next != block) MarkEmitted(next, m_Ctx.builder().GetInsertBlock());
			next = EmitBlock(next);
		}
	}

	MirBlockId MirEmitter::EmitBlock(MirBlockId id)
	{
		m_Current = id;
		const MirBlock& block = m_Mir.blocks[id];
		if (block.handler != c_MirNone)
		{
			m_Ctx.At(m_Mir.handlers[block.handler].at);
			m_Eh->BeginHandler(block.handler);
		}
		if (m_Blocks[id].cleanupOf != c_MirNone)
		{
			m_Ctx.At(m_Mir.steps[m_Blocks[id].cleanupOf].at);
			m_Eh->BeginCleanup(m_Blocks[id].cleanupOf);
		}
		for (const MirStatement& statement : block.statements)
		{
			if (!IsOpen()) break; // a noreturn asm ended the block
			m_Ctx.At(statement.at);
			m_StatementRegions.push_back({ statement.firstRegion, statement.regionCount });
			EmitStatement(statement);
			m_StatementRegions.pop_back();
		}
		if (!IsOpen()) return c_MirNone;
		m_Ctx.At(block.end.at);
		m_StatementRegions.push_back({ block.end.firstRegion, block.end.regionCount });
		const MirBlockId next = EmitTerminator(block);
		m_StatementRegions.pop_back();
		return next;
	}

	bool MirEmitter::IsOpen() const
	{
		llvm::BasicBlock* b = m_Ctx.builder().GetInsertBlock();
		return b && (b->empty() || !b->back().isTerminator());
	}

	// What emission leaves without a terminator cannot run: the rest of an expression after a region that never ends.
	void MirEmitter::TerminateOpenBlocks()
	{
		llvm::Function* fn = m_Ctx.builder().GetInsertBlock()->getParent();
		for (llvm::BasicBlock& bb : *fn)
			if (bb.empty() || !bb.back().isTerminator())
			{
				m_Ctx.builder().SetInsertPoint(&bb);
				m_Ctx.builder().CreateUnreachable();
			}
	}

	// --- what a call and an expression ask -------------------------------------------------------------------------

	llvm::BasicBlock* MirEmitter::UnwindTarget()
	{
		const MirBlock& block = m_Mir.blocks[m_Current];
		return block.mayUnwind ? m_Eh->UnwindBlock(block.unwind) : nullptr;
	}

	llvm::FuncletPadInst* MirEmitter::CurrentFunclet()
	{
		return m_Eh->Pad(m_Mir.blocks[m_Current].funclet);
	}

	// A block an expression runs in place: its region's blocks, from the insert point. The expression goes on where the
	// region ended (a region that never ends leaves it in a block that cannot run).
	void MirEmitter::EmitNestedBlock(const BlockNode* block)
	{
		const MirRegion* region = nullptr;
		if (!m_StatementRegions.empty())
		{
			const auto [first, count] = m_StatementRegions.back();
			for (uint32 i = 0; i < count && !region; ++i)
				if (m_Mir.regions[first + i].block == block) region = &m_Mir.regions[first + i];
		}
		if (!region)
		{
			// The MIR builder made a region for every block an expression runs (MirBuilder::BuildRegions, the operands
			// HIRWalk follows): one it did not is a fault of the compiler, which stops the program loudly here.
			llvm::Function* trap = llvm::Intrinsic::getOrInsertDeclaration(&m_Ctx.mctx().module(), llvm::Intrinsic::trap);
			m_Ctx.builder().CreateCall(trap);
			return;
		}
		const MirBlockId outer = m_Current;
		RegionEmission emission;
		emission.blocks.push_back({ region->entry, m_Ctx.builder().GetInsertBlock() });
		m_Regions.push_back(&emission);
		EmitFrom(region->entry);
		for (size_t i = 0; i < emission.pending.size(); ++i)
		{
			const MirBlockId next = emission.pending[i];
			m_Ctx.SetInsertPoint(emission.Find(next));
			EmitFrom(next);
		}
		m_Regions.pop_back();
		m_Current = outer;
		m_Ctx.SetInsertPoint(emission.end ? emission.end : NewBlock("unreachable.code"));
	}

	// --- statements -----------------------------------------------------------------------------------------------

	void MirEmitter::EmitStatement(const MirStatement& statement)
	{
		switch (statement.kind)
		{
		case MirStatement::Kind::Eval: m_Expr.Emit(statement.node); return;
		case MirStatement::Kind::Declare: EmitDeclare(static_cast<const LocalDeclNode*>(statement.node)); return;
		case MirStatement::Kind::Asm: AsmEmitter(m_Ctx, m_Expr).Emit(static_cast<const AsmNode*>(statement.node)); return;
		case MirStatement::Kind::AtExit: EmitAtExit(static_cast<const AtExitNode*>(statement.node)); return;
		case MirStatement::Kind::SetFlag:
			m_Ctx.builder().CreateStore(llvm::ConstantInt::getBool(m_Ctx.mctx().context(), statement.flag), m_Ctx.SlotAlloca(statement.slot));
			return;
		case MirStatement::Kind::StageReturn: EmitStageReturn(statement.node); return;
		case MirStatement::Kind::OnceEnd: EmitOnceEnd(static_cast<const OnceNode*>(statement.node)); return;
		}
	}

	void MirEmitter::EmitDeclare(const LocalDeclNode* d)
	{
		// A declaration without an initializer starts zeroed (with its vptrs) every time it runs, not only on the first
		// pass through a loop. A static local is program storage: a once region starts it.
		if (m_Ctx.SlotIsStatic(d->slot)) return;
		const bool isVolatile = m_Ctx.hir().GetSlot(d->slot).isVolatile;
		if (!d->init) m_Ctx.ResetSlot(d->slot);
		else if (!isVolatile && m_Expr.BuildsInPlace(d->init, d->type)) m_Expr.EmitInPlace(d->init, m_Ctx.SlotAlloca(d->slot)); // copy elision
		else m_Expr.GcStore(m_Ctx.SlotAlloca(d->slot), m_Expr.Emit(d->init), d->type, MemoryAccess{ isVolatile });
	}

	void MirEmitter::EmitAtExit(const AtExitNode* n)
	{
		ModuleContext& m = m_Ctx.mctx();
		llvm::Function* destroy = StaticStorage::ExitDestroyer(m, *n->destroy, n->count, n->stride);
		llvm::Function* registration = n->perThread ? StaticStorage::ThreadAtExit(m) : StaticStorage::AtExit(m);
		m_Ctx.Call(registration, { destroy, m_Expr.LValueAddress(n->object) }, false);
	}

	// A return's value, computed before the cleanups it crosses run (C++ order), staged in memory: SSA values do not
	// survive a catchret out of a funclet, and an sret result goes straight to its out-pointer.
	void MirEmitter::EmitStageReturn(const HIRNode* value)
	{
		auto& B = m_Ctx.builder();
		if (m_Ctx.Sret() && m_Expr.BuildsInPlace(value, ReturnType()))
		{
			m_Expr.EmitInPlace(value, m_Ctx.Sret()); // copy elision
			return;
		}
		llvm::Value* v = value ? m_Expr.Emit(value) : nullptr;
		if (!v) return;
		if (m_Ctx.Sret())
		{
			B.CreateStore(v, m_Ctx.Sret());
			return;
		}
		m_StagedType = v->getType();
		B.CreateStore(v, m_Ctx.ReturnSlot(m_StagedType));
	}

	void MirEmitter::EmitOnceEnd(const OnceNode* once)
	{
		llvm::Value* guard = OnceGuard(once);
		if (once->perThread) m_Ctx.builder().CreateStore(m_Ctx.builder().getInt32(StaticStorage::kOnceDone), guard);
		else m_Ctx.Call(StaticStorage::OnceEnd(m_Ctx.mctx()), { guard }, false);
	}

	// --- terminators ----------------------------------------------------------------------------------------------

	MirBlockId MirEmitter::EmitTerminator(const MirBlock& block)
	{
		auto& B = m_Ctx.builder();
		const MirTerminator& end = block.end;
		switch (end.kind)
		{
		case MirTerminator::Kind::Goto:
			if (Fuses(end.target)) return end.target; // its only way in: it goes on in this block
			B.CreateBr(TargetOf(end.target));
			return c_MirNone;
		case MirTerminator::Kind::Branch:
		{
			llvm::Value* condition = IRContext::AsBool(B, m_Expr.Emit(end.node));
			B.CreateCondBr(condition, TargetOf(end.target), TargetOf(end.otherwise));
			return c_MirNone;
		}
		case MirTerminator::Kind::Switch:
		{
			llvm::Value* value = m_Expr.Emit(end.node);
			auto* choice = B.CreateSwitch(value, TargetOf(end.otherwise), end.caseCount);
			for (uint32 i = 0; i < end.caseCount; ++i)
			{
				const MirSwitchCase& entry = m_Mir.cases[end.firstCase + i];
				choice->addCase(llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(value->getType()), entry.value), TargetOf(entry.target));
			}
			return c_MirNone;
		}
		case MirTerminator::Kind::Return: EmitReturn(end.node); return c_MirNone;
		case MirTerminator::Kind::ReturnStaged:
			EmitReturnValue(m_StagedType ? B.CreateLoad(m_StagedType, m_Ctx.ReturnSlot(m_StagedType)) : nullptr);
			return c_MirNone;
		case MirTerminator::Kind::FallOff: EmitFallOff(); return c_MirNone;
		case MirTerminator::Kind::Throw: m_Eh->Throw(static_cast<const ThrowNode*>(end.node), end.handler); return c_MirNone;
		case MirTerminator::Kind::Unreachable: B.CreateUnreachable(); return c_MirNone;
		case MirTerminator::Kind::LeaveHandler: m_Eh->LeaveHandler(end.handler, TargetOf(end.target)); return c_MirNone;
		case MirTerminator::Kind::Unwind: m_Eh->Unwind(block.funclet, end.step); return c_MirNone;
		case MirTerminator::Kind::OnceBegin:
			if (static_cast<const OnceNode*>(end.node)->perThread) EmitThreadOnceBegin(end);
			else EmitOnceBegin(end);
			return c_MirNone;
		case MirTerminator::Kind::RegionEnd:
			if (!m_Regions.empty()) m_Regions.back()->end = B.GetInsertBlock();
			return c_MirNone;
		case MirTerminator::Kind::Terminate: m_Eh->Terminate(); return c_MirNone;
		case MirTerminator::Kind::Open: B.CreateUnreachable(); return c_MirNone; // the builder ends every block
		}
		return c_MirNone;
	}

	const Type* MirEmitter::ReturnType() const
	{
		const FunctionRecord* record = m_Ctx.hir().record;
		return record && record->type ? record->type->GetReturnType() : nullptr;
	}

	void MirEmitter::EmitReturn(const HIRNode* value)
	{
		if (m_Ctx.Sret() && m_Expr.BuildsInPlace(value, ReturnType()))
		{
			m_Expr.EmitInPlace(value, m_Ctx.Sret()); // copy elision
			EmitReturnValue(nullptr);
			return;
		}
		EmitReturnValue(value ? m_Expr.Emit(value) : nullptr);
	}

	void MirEmitter::EmitReturnValue(llvm::Value* v)
	{
		auto& B = m_Ctx.builder();
		llvm::Function* fn = B.GetInsertBlock()->getParent();
		if (m_Ctx.Sret())
		{
			if (v) B.CreateStore(v, m_Ctx.Sret());
			B.CreateRetVoid();
			return;
		}
		if (!v)
		{
			if (fn->getReturnType()->isVoidTy()) B.CreateRetVoid();
			else B.CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
			return;
		}
		// An aggregate returned in registers returns as its RegisterType (the mirror of EmitCall's unpack).
		Abi& abi = m_Ctx.mctx().abi();
		const Type* ret = ReturnType();
		llvm::Type* registers = ret ? abi.RegisterType(ret) : nullptr;
		if (!registers)
		{
			B.CreateRet(v);
			return;
		}
		llvm::Value* tmp = m_Ctx.Materialize(v, abi.CoercionSlot(v->getType(), registers));
		B.CreateRet(B.CreateLoad(registers, tmp));
	}

	// Running off the end: a void body returns; `main` returns 0; any other function that returns a value cannot get
	// here (D-55: lowering rejects a body that could).
	void MirEmitter::EmitFallOff()
	{
		auto& B = m_Ctx.builder();
		llvm::Function* fn = B.GetInsertBlock()->getParent();
		const FunctionRecord* record = m_Ctx.hir().record;
		if (m_Ctx.Sret() || fn->getReturnType()->isVoidTy()) B.CreateRetVoid();
		else if (record && IsEntryPoint(*record, "main")) B.CreateRet(llvm::Constant::getNullValue(fn->getReturnType()));
		else B.CreateUnreachable();
	}

	llvm::Value* MirEmitter::OnceGuard(const OnceNode* once)
	{
		for (const auto& [node, made] : m_OnceGuards)
			if (node == once) return made;
		ModuleContext& m = m_Ctx.mctx();
		const Core::String fnMangled = m_Ctx.hir().record ? m_Ctx.hir().record->mangledName : "fn";
		llvm::Value* guard = once->perThread
			? static_cast<llvm::Value*>(m_Ctx.builder().CreateThreadLocalAddress(StaticStorage::NewThreadOnceGuard(m, fnMangled + "." + once->m_Name)))
			: StaticStorage::NewOnceGuard(m, fnMangled + "." + once->m_Name);
		m_OnceGuards.push_back({ once, guard });
		return guard;
	}

	// Every pass after the first costs one acquire load of the guard's state.
	void MirEmitter::EmitOnceBegin(const MirTerminator& end)
	{
		auto& B = m_Ctx.builder();
		auto* guard = llvm::cast<llvm::GlobalVariable>(OnceGuard(static_cast<const OnceNode*>(end.node)));
		llvm::BasicBlock* beginBB = NewBlock("once.begin");
		llvm::BasicBlock* runBB = TargetOf(end.target);
		llvm::BasicBlock* doneBB = TargetOf(end.otherwise);
		B.CreateCondBr(StaticStorage::IsOnceDone(B, guard), doneBB, beginBB);
		B.SetInsertPoint(beginBB);
		llvm::Value* run = m_Ctx.Call(StaticStorage::OnceBegin(m_Ctx.mctx()), { guard }, false);
		B.CreateCondBr(B.CreateICmpNE(run, llvm::ConstantInt::get(run->getType(), 0)), runBB, doneBB);
	}

	// No other thread shares the guard: each pass after a thread's first costs that thread one load, no lock.
	void MirEmitter::EmitThreadOnceBegin(const MirTerminator& end)
	{
		auto& B = m_Ctx.builder();
		ModuleContext& m = m_Ctx.mctx();
		llvm::Value* guard = OnceGuard(static_cast<const OnceNode*>(end.node));
		llvm::BasicBlock* notDoneBB = NewBlock("thread.once.notdone");
		llvm::BasicBlock* reenteredBB = NewBlock("thread.once.reentered");
		llvm::BasicBlock* startBB = NewBlock("thread.once.run");
		llvm::BasicBlock* doneBB = TargetOf(end.otherwise);
		llvm::Value* state = B.CreateLoad(B.getInt32Ty(), guard, "thread.once.state");
		B.CreateCondBr(B.CreateICmpEQ(state, B.getInt32(StaticStorage::kOnceDone)), doneBB, notDoneBB);
		B.SetInsertPoint(notDoneBB);
		B.CreateCondBr(B.CreateICmpEQ(state, B.getInt32(StaticStorage::kOnceRunning)), reenteredBB, startBB);
		B.SetInsertPoint(reenteredBB);
		m_Ctx.Call(StaticStorage::ThreadOnceReentered(m), {}, false);
		B.CreateUnreachable();
		B.SetInsertPoint(startBB);
		B.CreateStore(B.getInt32(StaticStorage::kOnceRunning), guard);
		B.CreateBr(TargetOf(end.target));
	}
} // namespace C4Compiler
