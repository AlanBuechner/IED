#include "Backend/CodeGen/RegionEmitter.h"
#include "Backend/CodeGen/AsmEmitter.h"
#include "Backend/CodeGen/CallEmitter.h"
#include "Backend/CodeGen/EhTables.h"
#include "Backend/CodeGen/StaticStorage.h"
#include "Model/TypeHandle.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>

namespace Scripting
{
	namespace
	{
		constexpr uint32_t kCatchAllAdjectives = 64; // catchpad [null, 64, null] is `catch (...)`
	}

	RegionEmitter::RegionEmitter(IRContext& ctx) : m_Ctx(ctx), m_Expr(ctx)
	{
		ctx.SetRegionServices(this);
	}

	RegionEmitter::~RegionEmitter()
	{
		m_Ctx.SetRegionServices(nullptr);
	}

	bool RegionEmitter::IsOpen() const
	{
		llvm::BasicBlock* b = m_Ctx.builder().GetInsertBlock();
		return b && (b->empty() || !b->back().isTerminator());
	}

	llvm::BasicBlock* RegionEmitter::NewBlock(const char* name)
	{
		return llvm::BasicBlock::Create(m_Ctx.mctx().context(), name, m_Ctx.builder().GetInsertBlock()->getParent());
	}

	void RegionEmitter::ContinueAfterTerminator()
	{
		// Statements after a terminator are unreachable but still emitted: a later label can make them live.
		m_Ctx.SetInsertPoint(NewBlock("unreachable.code"));
	}

	// --- blocks and scope exit --------------------------------------------------------------------------

	void RegionEmitter::EmitBlock(const BlockNode* block)
	{
		size_t start = m_Ctx.Cleanups().Depth();
		m_Frames.push_back({ block, start });
		DeclareLabels(block);
		for (const HIRNode* stmt : block->stmts)
		{
			m_Frames.back().statementDepth = m_Ctx.Cleanups().Depth();
			EmitStatement(stmt);
		}
		EndScope(start);
		m_Frames.pop_back();
	}

	void RegionEmitter::PushCleanup(const BlockNode* body, bool exceptionalOnly)
	{
		CleanupEntry e;
		e.kind = CleanupEntry::Kind::Cleanup;
		e.body = body;
		e.exceptionalOnly = exceptionalOnly;
		e.parentPad = m_Ctx.CurrentFunclet();
		e.floor = m_Ctx.Floor();
		m_Ctx.Cleanups().Push(e);
	}

	void RegionEmitter::PushAbandon(llvm::Function* abandon, llvm::Value* argument)
	{
		CleanupEntry e;
		e.kind = CleanupEntry::Kind::Abandon;
		e.abandon = abandon;
		e.abandonArgument = argument;
		e.parentPad = m_Ctx.CurrentFunclet();
		e.floor = m_Ctx.Floor();
		m_Ctx.Cleanups().Push(e);
	}

	void RegionEmitter::ExitEntry(const CleanupEntry& e)
	{
		if (!IsOpen()) return;
		if (e.kind == CleanupEntry::Kind::Cleanup)
		{
			if (!e.exceptionalOnly) EmitBlock(e.body);
		}
		else if (e.kind == CleanupEntry::Kind::Handler)
		{
			// Leaving a catch handler by a jump: return from its funclet first, then continue outside it.
			llvm::BasicBlock* after = NewBlock("catchret.dest");
			m_Ctx.builder().CreateCatchRet(e.handlerPad, after);
			m_Ctx.SetInsertPoint(after);
		}
	}

	void RegionEmitter::ExitCleanups(size_t depth)
	{
		// Each crossed entry is popped while its exit is emitted, so code in a cleanup body sees exactly the
		// scopes outside it (its calls unwind past it, not into it); the stack is restored afterwards because
		// the scopes are still open for the code that follows the jump.
		CleanupStack& cs = m_Ctx.Cleanups();
		std::vector<CleanupEntry> crossed;
		while (cs.Depth() > depth)
		{
			crossed.push_back(cs.Pop());
			ExitEntry(crossed.back());
		}
		for (auto it = crossed.rbegin(); it != crossed.rend(); ++it) cs.Push(*it);
	}

	void RegionEmitter::EndScope(size_t depth)
	{
		CleanupStack& cs = m_Ctx.Cleanups();
		while (cs.Depth() > depth)
		{
			CleanupEntry e = cs.Pop();
			ExitEntry(e);
		}
	}

	void RegionEmitter::BranchThroughCleanups(size_t depth, llvm::BasicBlock* target)
	{
		ExitCleanups(depth);
		if (IsOpen()) m_Ctx.builder().CreateBr(target);
		ContinueAfterTerminator();
	}

	size_t RegionEmitter::ForwardJumpDepth(const BlockNode* owner) const
	{
		// A forward goto keeps the cleanups its label's block registered before the statement the goto is in
		// (HIR guarantees no declaration or defer lies between), and leaves everything nested inside it.
		for (size_t f = m_Frames.size(); f-- > 0; )
			if (m_Frames[f].block == owner) return m_Frames[f].statementDepth;
		return m_Ctx.Cleanups().Depth();
	}

	// --- labels -----------------------------------------------------------------------------------------

	void RegionEmitter::DeclareLabels(const BlockNode* block)
	{
		for (const HIRNode* stmt : block->stmts)
			if (stmt->kind == HIRNode::Kind::Label)
			{
				Label& l = m_Labels[static_cast<const LabelNode*>(stmt)->labelId];
				if (!l.block) l.block = NewBlock("label");
				l.owner = block;
				l.placed = false;
			}
	}

	void RegionEmitter::DefineLabel(uint32 id, llvm::BasicBlock* block)
	{
		Label& l = m_Labels[id];
		l.block = block;
		l.depth = m_Ctx.Cleanups().Depth();
		l.placed = true;
		l.owner = nullptr;
	}

	void RegionEmitter::EmitGoto(const GotoNode* g)
	{
		Label& l = m_Labels[g->targetLabel];
		if (!l.block) l.block = NewBlock("label");
		size_t depth = l.placed ? l.depth : ForwardJumpDepth(l.owner);
		BranchThroughCleanups(depth, l.block);
	}

	void RegionEmitter::EmitLabel(const LabelNode* n)
	{
		Label& l = m_Labels[n->labelId];
		if (!l.block) l.block = NewBlock("label");
		if (IsOpen()) m_Ctx.builder().CreateBr(l.block); // falling through into the label continues there
		m_Ctx.SetInsertPoint(l.block);
		l.depth = m_Ctx.Cleanups().Depth();
		l.placed = true;
	}

	// --- statements -------------------------------------------------------------------------------------

	void RegionEmitter::EmitStatement(const HIRNode* s)
	{
		m_Ctx.At(s->origin);
		switch (s->kind)
		{
		case HIRNode::Kind::LocalDecl: EmitLocalDecl(static_cast<const LocalDeclNode*>(s)); return;
		case HIRNode::Kind::Store:     m_Expr.Emit(s); return;
		case HIRNode::Kind::ExprStmt:  m_Expr.Emit(static_cast<const ExprStmtNode*>(s)->expr); return;
		case HIRNode::Kind::Block:     EmitBlock(static_cast<const BlockNode*>(s)); return;
		case HIRNode::Kind::Defer:
		{
			const auto* d = static_cast<const DeferNode*>(s);
			PushCleanup(d->body, d->exceptionalOnly);
			return;
		}
		case HIRNode::Kind::Return:    EmitReturn(static_cast<const ReturnNode*>(s)); return;
		case HIRNode::Kind::Break:
		case HIRNode::Kind::Continue:
		case HIRNode::Kind::Jump:
		{
			uint32 id = s->kind == HIRNode::Kind::Break ? static_cast<const BreakNode*>(s)->targetLabel
				: s->kind == HIRNode::Kind::Continue ? static_cast<const ContinueNode*>(s)->targetLabel
				: static_cast<const JumpNode*>(s)->targetLabel;
			const Label& l = m_Labels[id];
			BranchThroughCleanups(l.depth, l.block);
			return;
		}
		case HIRNode::Kind::Goto:      EmitGoto(static_cast<const GotoNode*>(s)); return;
		case HIRNode::Kind::Label:     EmitLabel(static_cast<const LabelNode*>(s)); return;
		case HIRNode::Kind::If:        EmitIf(static_cast<const IfNode*>(s)); return;
		case HIRNode::Kind::While:     EmitWhile(static_cast<const WhileNode*>(s)); return;
		case HIRNode::Kind::For:       EmitFor(static_cast<const ForNode*>(s)); return;
		case HIRNode::Kind::Switch:    EmitSwitch(static_cast<const SwitchNode*>(s)); return;
		case HIRNode::Kind::TryCatch:  EmitTryCatch(static_cast<const TryNode*>(s)); return;
		case HIRNode::Kind::Throw:     EmitThrow(static_cast<const ThrowNode*>(s)); return;
		case HIRNode::Kind::Once:      EmitOnce(static_cast<const OnceNode*>(s)); return;
		case HIRNode::Kind::AtExit:    EmitAtExit(static_cast<const AtExitNode*>(s)); return;
		case HIRNode::Kind::Asm:
			if (!AsmEmitter(m_Ctx, m_Expr).Emit(static_cast<const AsmNode*>(s))) ContinueAfterTerminator();
			return;
		default: return;
		}
	}

	void RegionEmitter::EmitLocalDecl(const LocalDeclNode* d)
	{
		// A declaration without an initializer starts zeroed (with its vptrs) every time it runs, not only
		// on the first pass through a loop. A static local is program storage: a Once region starts it.
		if (m_Ctx.SlotIsStatic(d->slot)) return;
		if (!d->init) m_Ctx.ResetSlot(d->slot);
		else m_Expr.GcStore(m_Ctx.SlotAlloca(d->slot), m_Expr.Emit(d->init), d->type,
			MemoryAccess{ m_Ctx.hir().GetSlot(d->slot).isVolatile });
		// A local that a later move may take has a drop flag: it owns its object from here (LastUseMoves).
		if (d->liveFlag != kNoDropFlag)
			m_Ctx.builder().CreateStore(llvm::ConstantInt::getTrue(m_Ctx.mctx().context()), m_Ctx.SlotAlloca(d->liveFlag));
	}

	void RegionEmitter::EmitOnce(const OnceNode* n)
	{
		if (n->perThread) { EmitThreadOnce(n); return; }
		// Every pass after the first costs one acquire load of the guard's state.
		auto& B = m_Ctx.builder();
		ModuleContext& m = m_Ctx.mctx();
		const Core::String fnMangled = m_Ctx.hir().record ? m_Ctx.hir().record->mangledName : "fn";
		llvm::GlobalVariable* guard = StaticStorage::NewOnceGuard(m, fnMangled + "." + n->m_Name);
		llvm::BasicBlock* beginBB = NewBlock("once.begin");
		llvm::BasicBlock* runBB = NewBlock("once.run");
		llvm::BasicBlock* doneBB = NewBlock("once.done");
		B.CreateCondBr(StaticStorage::IsOnceDone(B, guard), doneBB, beginBB);
		B.SetInsertPoint(beginBB);
		llvm::Value* run = m_Ctx.Call(StaticStorage::OnceBegin(m), { guard }, false);
		B.CreateCondBr(B.CreateICmpNE(run, llvm::ConstantInt::get(run->getType(), 0)), runBB, doneBB);
		B.SetInsertPoint(runBB);
		const size_t depth = m_Ctx.Cleanups().Depth();
		PushAbandon(StaticStorage::OnceAbandon(m), guard);
		EmitBlock(n->body);
		EndScope(depth);
		if (IsOpen())
		{
			m_Ctx.Call(StaticStorage::OnceEnd(m), { guard }, false);
			B.CreateBr(doneBB);
		}
		B.SetInsertPoint(doneBB);
	}

	void RegionEmitter::EmitThreadOnce(const OnceNode* n)
	{
		// No other thread shares the guard: each pass after a thread's first costs that thread one load, no lock.
		auto& B = m_Ctx.builder();
		ModuleContext& m = m_Ctx.mctx();
		const Core::String fnMangled = m_Ctx.hir().record ? m_Ctx.hir().record->mangledName : "fn";
		llvm::Value* guard = B.CreateThreadLocalAddress(StaticStorage::NewThreadOnceGuard(m, fnMangled + "." + n->m_Name));
		llvm::BasicBlock* notDoneBB = NewBlock("thread.once.notdone");
		llvm::BasicBlock* reenteredBB = NewBlock("thread.once.reentered");
		llvm::BasicBlock* runBB = NewBlock("thread.once.run");
		llvm::BasicBlock* doneBB = NewBlock("thread.once.done");
		llvm::Value* state = B.CreateLoad(B.getInt32Ty(), guard, "thread.once.state");
		B.CreateCondBr(B.CreateICmpEQ(state, B.getInt32(StaticStorage::kOnceDone)), doneBB, notDoneBB);
		B.SetInsertPoint(notDoneBB);
		B.CreateCondBr(B.CreateICmpEQ(state, B.getInt32(StaticStorage::kOnceRunning)), reenteredBB, runBB);
		B.SetInsertPoint(reenteredBB);
		m_Ctx.Call(StaticStorage::ThreadOnceReentered(m), {}, false);
		B.CreateUnreachable();
		B.SetInsertPoint(runBB);
		B.CreateStore(B.getInt32(StaticStorage::kOnceRunning), guard);
		const size_t depth = m_Ctx.Cleanups().Depth();
		PushAbandon(StaticStorage::ThreadOnceAbandon(m), guard);
		EmitBlock(n->body);
		EndScope(depth);
		if (IsOpen())
		{
			B.CreateStore(B.getInt32(StaticStorage::kOnceDone), guard);
			B.CreateBr(doneBB);
		}
		B.SetInsertPoint(doneBB);
	}

	void RegionEmitter::EmitAtExit(const AtExitNode* n)
	{
		ModuleContext& m = m_Ctx.mctx();
		llvm::Function* destroy = StaticStorage::ExitDestroyer(m, *n->destroy, n->count, n->stride);
		llvm::Function* registration = n->perThread ? StaticStorage::ThreadAtExit(m) : StaticStorage::AtExit(m);
		m_Ctx.Call(registration, { destroy, m_Expr.LValueAddress(n->object) }, false);
	}

	void RegionEmitter::EmitReturn(const ReturnNode* r)
	{
		auto& B = m_Ctx.builder();
		const FunctionRecord* rec = m_Ctx.hir().record;
		const Type* ret = rec && rec->type ? rec->type->GetReturnType() : nullptr;
		llvm::Value* v = nullptr;
		if (const CallNode* call = ResultCall(r)) CallEmitter(m_Ctx, m_Expr).EmitInto(call, m_Ctx.Sret()); // copy elision
		else if (r->value) v = m_Expr.Emit(r->value);
		if (m_Ctx.Cleanups().Depth() == 0)
		{
			EmitReturnValue(v, ret);
			ContinueAfterTerminator();
			return;
		}
		// The value is computed before the scope's cleanups run (C++ order) and staged in memory: SSA values
		// do not survive a catchret out of a funclet, and an sret result goes straight to its out-pointer.
		llvm::Value* staged = nullptr;
		if (v && m_Ctx.Sret())
			B.CreateStore(v, m_Ctx.Sret());
		else if (v)
		{
			staged = m_Ctx.ReturnSlot(v->getType());
			B.CreateStore(v, staged);
		}
		ExitCleanups(0);
		if (IsOpen())
			EmitReturnValue(staged ? B.CreateLoad(v->getType(), staged) : nullptr, ret);
		ContinueAfterTerminator();
	}

	const CallNode* RegionEmitter::ResultCall(const ReturnNode* r)
	{
		if (!m_Ctx.Sret() || !r->value || r->value->kind != HIRNode::Kind::Call) return nullptr;
		const auto* call = static_cast<const CallNode*>(r->value);
		const FunctionRecord* rec = m_Ctx.hir().record;
		const Type* ret = rec && rec->type ? rec->type->GetReturnType() : nullptr;
		if (!ret || TypeHandle::UnwrapToValue(call->type) != TypeHandle::UnwrapToValue(ret)) return nullptr;
		return CallEmitter(m_Ctx, m_Expr).ReturnsInMemory(call) ? call : nullptr;
	}

	void RegionEmitter::EmitReturnValue(llvm::Value* v, const Type* ret)
	{
		auto& B = m_Ctx.builder();
		llvm::Function* fn = B.GetInsertBlock()->getParent();
		Abi& abi = m_Ctx.mctx().abi();
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
		// A 1/2/4/8-byte aggregate returns as an integer of its width (the mirror of EmitCall's unpack).
		uint32_t intSize = ret ? abi.ByValueIntSize(ret) : 0;
		if (intSize == 0) { B.CreateRet(v); return; }
		llvm::Value* tmp = m_Ctx.Materialize(v, v->getType());
		B.CreateRet(B.CreateLoad(llvm::IntegerType::get(m_Ctx.mctx().context(), intSize * 8), tmp));
	}

	// --- control flow -----------------------------------------------------------------------------------

	void RegionEmitter::EmitIf(const IfNode* n)
	{
		auto& B = m_Ctx.builder();
		llvm::Value* cond = IRContext::AsBool(B, m_Expr.Emit(n->cond));
		llvm::BasicBlock* thenBB = NewBlock("if.then");
		llvm::BasicBlock* elseBB = n->elseBlock ? NewBlock("if.else") : nullptr;
		llvm::BasicBlock* joinBB = NewBlock("if.cont");
		B.CreateCondBr(cond, thenBB, elseBB ? elseBB : joinBB);

		B.SetInsertPoint(thenBB);
		if (n->thenBlock) EmitBlock(n->thenBlock);
		if (IsOpen()) B.CreateBr(joinBB);
		if (elseBB)
		{
			B.SetInsertPoint(elseBB);
			EmitBlock(n->elseBlock);
			if (IsOpen()) B.CreateBr(joinBB);
		}
		B.SetInsertPoint(joinBB);
	}

	void RegionEmitter::EmitWhile(const WhileNode* n)
	{
		auto& B = m_Ctx.builder();
		llvm::BasicBlock* headerBB = NewBlock("loop.header");
		llvm::BasicBlock* bodyBB = NewBlock("loop.body");
		llvm::BasicBlock* exitBB = NewBlock("loop.exit");
		DefineLabel(n->breakLabel, exitBB);
		DefineLabel(n->continueLabel, headerBB);
		B.CreateBr(n->isDoWhile ? bodyBB : headerBB);

		B.SetInsertPoint(headerBB);
		B.CreateCondBr(IRContext::AsBool(B, m_Expr.Emit(n->cond)), bodyBB, exitBB);
		B.SetInsertPoint(bodyBB);
		EmitBlock(n->body);
		if (IsOpen()) B.CreateBr(headerBB);
		B.SetInsertPoint(exitBB);
	}

	void RegionEmitter::EmitFor(const ForNode* n)
	{
		// The init block is the scope of the whole loop: its cleanups run when the loop is left, not when the
		// init clause ends.
		size_t initDepth = m_Ctx.Cleanups().Depth();
		if (!n->initBlock) { EmitForLoop(n); return; }
		m_Frames.push_back({ n->initBlock, initDepth });
		for (const HIRNode* stmt : n->initBlock->stmts)
		{
			m_Frames.back().statementDepth = m_Ctx.Cleanups().Depth();
			EmitStatement(stmt);
		}
		m_Frames.back().statementDepth = m_Ctx.Cleanups().Depth();
		EmitForLoop(n);
		EndScope(initDepth);
		m_Frames.pop_back();
	}

	void RegionEmitter::EmitForLoop(const ForNode* n)
	{
		auto& B = m_Ctx.builder();
		llvm::BasicBlock* headerBB = NewBlock("for.header");
		llvm::BasicBlock* bodyBB = NewBlock("for.body");
		llvm::BasicBlock* stepBB = NewBlock("for.step");
		llvm::BasicBlock* exitBB = NewBlock("for.exit");
		DefineLabel(n->breakLabel, exitBB);
		DefineLabel(n->continueLabel, stepBB);
		B.CreateBr(headerBB);

		B.SetInsertPoint(headerBB);
		if (n->cond) B.CreateCondBr(IRContext::AsBool(B, m_Expr.Emit(n->cond)), bodyBB, exitBB);
		else B.CreateBr(bodyBB);
		B.SetInsertPoint(bodyBB);
		EmitBlock(n->body);
		if (IsOpen()) B.CreateBr(stepBB);
		B.SetInsertPoint(stepBB);
		if (n->step) m_Expr.Emit(n->step);
		if (IsOpen()) B.CreateBr(headerBB);
		B.SetInsertPoint(exitBB);
	}

	void RegionEmitter::EmitSwitch(const SwitchNode* n)
	{
		auto& B = m_Ctx.builder();
		llvm::BasicBlock* exitBB = NewBlock("switch.exit");
		DefineLabel(n->breakLabel, exitBB);
		// Each case has its own entry label; a fallthrough Jump targets the next case's entry.
		std::vector<llvm::BasicBlock*> caseBBs(n->cases.size());
		for (size_t i = 0; i < n->cases.size(); ++i)
		{
			caseBBs[i] = NewBlock("case");
			DefineLabel(n->cases[i].entryLabel, caseBBs[i]);
		}
		llvm::BasicBlock* defaultBB = n->defaultBlock ? NewBlock("switch.default") : exitBB;
		if (n->defaultBlock) DefineLabel(n->defaultEntry, defaultBB);

		llvm::Value* cond = m_Expr.Emit(n->cond);
		auto* sw = B.CreateSwitch(cond, defaultBB, (unsigned)n->cases.size());
		for (size_t i = 0; i < n->cases.size(); ++i)
			sw->addCase(llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(cond->getType()), n->cases[i].value.AsInt()), caseBBs[i]);

		for (size_t i = 0; i < n->cases.size(); ++i)
		{
			B.SetInsertPoint(caseBBs[i]);
			EmitBlock(n->cases[i].block);
			if (IsOpen()) B.CreateBr(i + 1 < n->cases.size() ? caseBBs[i + 1] : exitBB);
		}
		if (n->defaultBlock)
		{
			B.SetInsertPoint(defaultBB);
			EmitBlock(n->defaultBlock);
			if (IsOpen()) B.CreateBr(exitBB);
		}
		B.SetInsertPoint(exitBB);
	}

	// --- exceptions -------------------------------------------------------------------------------------
	// A try is a catchswitch ("catch.dispatch") that every call in the try body unwinds to; each clause is a
	// catchpad funclet left with catchret. The frame handler matches the thrown object against each clause's
	// type descriptor in order. An exception no clause matches continues to whatever this try would itself
	// unwind to: the innermost enclosing cleanup pad or try, or the caller.

	void RegionEmitter::EmitTryCatch(const TryNode* n)
	{
		if (n->catches.empty()) { EmitBlock(n->tryBlock); return; }
		auto& B = m_Ctx.builder();
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		llvm::Value* parent = m_Ctx.CurrentFunclet();
		if (!parent) parent = llvm::ConstantTokenNone::get(ctx);
		llvm::BasicBlock* unwindDest = m_Ctx.UnwindTarget();

		llvm::BasicBlock* dispatchBB = NewBlock("catch.dispatch");
		llvm::BasicBlock* joinBB = NewBlock("try.cont");
		std::vector<llvm::BasicBlock*> handlers;
		for (size_t i = 0; i < n->catches.size(); ++i) handlers.push_back(NewBlock("catch"));
		llvm::CatchSwitchInst* dispatch = nullptr;
		{
			InsertGuard g(B, dispatchBB);
			dispatch = B.CreateCatchSwitch(parent, unwindDest, (unsigned)n->catches.size(), "catchswitch");
			for (llvm::BasicBlock* h : handlers) dispatch->addHandler(h);
		}

		size_t tryDepth = m_Ctx.Cleanups().Depth();
		CleanupEntry tryEntry;
		tryEntry.kind = CleanupEntry::Kind::Try;
		tryEntry.dispatch = dispatchBB;
		tryEntry.floor = m_Ctx.Floor();
		m_Ctx.Cleanups().Push(tryEntry);
		EmitBlock(n->tryBlock);
		if (IsOpen()) B.CreateBr(joinBB);
		m_Ctx.Cleanups().PopTo(tryDepth);

		for (size_t i = 0; i < n->catches.size(); ++i)
			EmitHandler(n->catches[i], dispatch, handlers[i], joinBB);
		B.SetInsertPoint(joinBB);
	}

	void RegionEmitter::EmitNoexceptBody(const BlockNode* root)
	{
		// The body of a noexcept function runs inside a catch-all whose handler stops the program: an
		// exception never leaves the function.
		auto& B = m_Ctx.builder();
		llvm::LLVMContext& ctx = m_Ctx.mctx().context();
		auto* ptr = llvm::PointerType::get(ctx, 0);
		llvm::BasicBlock* dispatchBB = NewBlock("noexcept.dispatch");
		llvm::BasicBlock* handlerBB = NewBlock("noexcept.terminate");
		{
			InsertGuard g(B, dispatchBB);
			llvm::CatchSwitchInst* dispatch = B.CreateCatchSwitch(llvm::ConstantTokenNone::get(ctx), nullptr, 1, "noexcept.switch");
			dispatch->addHandler(handlerBB);
			B.SetInsertPoint(handlerBB);
			llvm::CatchPadInst* pad = B.CreateCatchPad(dispatch, { llvm::ConstantPointerNull::get(ptr),
				llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), kCatchAllAdjectives), llvm::ConstantPointerNull::get(ptr) });
			llvm::Function* violation = m_Ctx.mctx().RuntimeFunction("c4_noexcept_violation", llvm::Type::getVoidTy(ctx), {});
			violation->setDoesNotReturn();
			B.CreateCall(violation, {}, { llvm::OperandBundleDef("funclet", std::vector<llvm::Value*>{ pad }) });
			B.CreateUnreachable();
		}
		size_t depth = m_Ctx.Cleanups().Depth();
		CleanupEntry entry;
		entry.kind = CleanupEntry::Kind::Try;
		entry.dispatch = dispatchBB;
		entry.floor = m_Ctx.Floor();
		m_Ctx.Cleanups().Push(entry);
		EmitBlock(root);
		m_Ctx.Cleanups().PopTo(depth);
	}

	void RegionEmitter::EmitHandler(const TryNode::CatchClause& clause, llvm::CatchSwitchInst* dispatch,
		llvm::BasicBlock* handlerBlock, llvm::BasicBlock* join)
	{
		// The frame handler copies the caught value (or, by reference, stores its address) into the clause's
		// slot; `catch (...)` has none.
		auto& B = m_Ctx.builder();
		B.SetInsertPoint(handlerBlock);
		m_Ctx.At(clause.origin); // the handler's entry is the catch's line, not the try body's last
		EhTables::Handler handler = m_Ctx.mctx().eh().HandlerFor(clause);
		llvm::Value* caught = clause.catchAll ? llvm::ConstantPointerNull::get(llvm::PointerType::get(m_Ctx.mctx().context(), 0))
			: m_Ctx.SlotAlloca(clause.slot);
		llvm::CatchPadInst* pad = B.CreateCatchPad(dispatch,
			{ handler.typeDescriptor, llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Ctx.mctx().context()), handler.adjectives), caught }, "catchpad");

		size_t handlerDepth = m_Ctx.Cleanups().Depth();
		CleanupEntry entry;
		entry.kind = CleanupEntry::Kind::Handler;
		entry.handlerPad = pad;
		entry.parentPad = pad;
		entry.floor = m_Ctx.Floor();
		m_Ctx.Cleanups().Push(entry);
		EmitBlock(clause.block);
		if (IsOpen()) B.CreateCatchRet(pad, join);
		m_Ctx.Cleanups().PopTo(handlerDepth);
	}

	void RegionEmitter::EmitThrow(const ThrowNode* n)
	{
		auto& B = m_Ctx.builder();
		ModuleContext& mc = m_Ctx.mctx();
		llvm::Function* raise = mc.GetCxxThrowException();
		if (n->isRethrow)
		{
			// Bare `throw;` is clang's funclet rethrow: _CxxThrowException(null, null) inside the active catchpad
			// re-raises the in-flight exception (value and type) unchanged.
			if (m_Ctx.CurrentHandlerPad())
			{
				llvm::Constant* null = llvm::ConstantPointerNull::get(llvm::PointerType::get(mc.context(), 0));
				m_Ctx.Call(raise, { null, null }, true);
			}
			B.CreateUnreachable();
			ContinueAfterTerminator();
			return;
		}
		const Type* vt = n->value ? n->value->type : nullptr;
		if (vt)
		{
			llvm::Value* val = m_Expr.Emit(n->value);
			llvm::Value* addr = m_Ctx.EnsureThrowSlot(vt);
			B.CreateStore(val, addr);
			m_Ctx.Call(raise, { addr, mc.eh().ThrowInfo(vt, { n->destroy, n->copies }) }, true);
		}
		B.CreateUnreachable();
		ContinueAfterTerminator();
	}

	llvm::BasicBlock* RegionEmitter::CleanupPadFor(size_t index)
	{
		// The exception-path copy of a cleanup: a cleanuppad that runs the body with only the scopes outside
		// it visible, then unwinds to whatever is below it (another cleanup's pad, a catchswitch, or the caller).
		CleanupStack& cs = m_Ctx.Cleanups();
		if (cs.At(index).ehPad) return cs.At(index).ehPad;
		llvm::BasicBlock* padBlock = NewBlock("ehcleanup");
		cs.At(index).ehPad = padBlock;
		const CleanupEntry entry = cs.At(index);
		llvm::Value* parent = entry.parentPad;
		size_t floor = entry.floor;
		llvm::BasicBlock* unwindTo = m_Ctx.UnwindTargetBelow(index, floor);

		auto& B = m_Ctx.builder();
		InsertGuard guard(B, padBlock);
		if (!parent) parent = llvm::ConstantTokenNone::get(m_Ctx.mctx().context());
		llvm::CleanupPadInst* pad = B.CreateCleanupPad(parent, {}, "cleanuppad");
		std::vector<CleanupEntry> above = cs.DetachFrom(index);
		IRContext::FuncletState saved = m_Ctx.EnterCleanupFunclet(index, pad);
		EmitCleanupBody(entry);
		if (IsOpen()) B.CreateCleanupRet(pad, unwindTo);
		m_Ctx.LeaveCleanupFunclet(saved);
		cs.Restore(std::move(above));
		return padBlock;
	}

	void RegionEmitter::EmitCleanupBody(const CleanupEntry& e)
	{
		if (e.kind == CleanupEntry::Kind::Abandon) m_Ctx.Call(e.abandon, { e.abandonArgument }, false);
		else EmitBlock(e.body);
	}
} // namespace Scripting
