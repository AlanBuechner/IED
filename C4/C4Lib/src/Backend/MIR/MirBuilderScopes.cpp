// MirBuilder's scopes: cleanups, how each exit runs them, a return's way out, an exception's (its unwind steps), try
// and catch, once regions. docs/compiler/06-mir.md "Scopes and exits".
#include "Backend/MIR/MirBuilder.h"

namespace C4Compiler
{
	// --- scopes and exits -------------------------------------------------------------------------------------

	void MirBuilder::PushCleanup(const DeferNode* defer)
	{
		Scope scope;
		scope.kind = Scope::Kind::Cleanup;
		scope.defer = defer;
		PushScope(scope);
	}

	void MirBuilder::PushScope(Scope scope)
	{
		scope.funclet = CurrentFunclet();
		scope.floor = m_Floor;
		m_Scopes.push_back(scope);
	}

	void MirBuilder::PopTo(size_t depth)
	{
		if (m_Scopes.size() > depth) m_Scopes.resize(depth);
	}

	void MirBuilder::EndScope(size_t depth)
	{
		while (m_Scopes.size() > depth)
		{
			const Scope scope = m_Scopes.back();
			m_Scopes.pop_back();
			ExitScope(scope);
		}
	}

	void MirBuilder::ExitCleanups(size_t depth)
	{
		// Each crossed scope is popped while its exit is built, so a cleanup's copy sees exactly the scopes outside it
		// (its calls unwind past it, not into it); the scopes come back after: they are still open for what follows.
		std::vector<Scope> crossed;
		while (m_Scopes.size() > depth)
		{
			crossed.push_back(m_Scopes.back());
			m_Scopes.pop_back();
			ExitScope(crossed.back());
		}
		for (auto it = crossed.rbegin(); it != crossed.rend(); ++it) m_Scopes.push_back(*it);
	}

	void MirBuilder::ExitScope(const Scope& scope)
	{
		if (!IsOpen()) return;
		if (scope.kind == Scope::Kind::Cleanup)
		{
			if (!scope.defer->exceptionalOnly) BuildCleanupCopy(scope.defer, nullptr);
			return;
		}
		if (scope.kind != Scope::Kind::Handler) return;
		// Leaving a catch handler by a jump or a return ends its catch first.
		MirTerminator leave;
		leave.kind = MirTerminator::Kind::LeaveHandler;
		leave.handler = scope.handler;
		leave.target = NewBlock();
		End(leave);
		Enter(leave.target);
	}

	void MirBuilder::BranchThroughCleanups(size_t depth, MirBlockId target)
	{
		ExitCleanups(depth);
		if (IsOpen()) Goto(target);
		ContinueAfterTerminator();
	}

	void MirBuilder::BuildCleanupCopy(const DeferNode* defer, const MirTerminator* exit)
	{
		const DeferNode* outer = m_CleanupOf;
		m_CleanupOf = defer;
		MirDropGuard guard;
		const bool guarded = m_Options && m_Options->guarded.count(defer) != 0;
		if (guarded)
		{
			guard.cleanup = defer;
			guard.guard = NewBlock();
			guard.body = NewBlock();
			guard.after = NewBlock();
			if (IsOpen()) Goto(guard.guard);
			Enter(guard.guard);
			Goto(guard.body);
			Enter(guard.body);
		}
		BuildBlock(defer->body);
		if (guarded)
		{
			if (IsOpen()) Goto(guard.after);
			Enter(guard.after);
			m_Body.dropGuards.push_back(guard);
		}
		m_CleanupOf = outer;
		if (exit && IsOpen()) End(*exit);
	}

	std::vector<MirBuilder::Scope> MirBuilder::DetachFrom(size_t depth)
	{
		std::vector<Scope> above(m_Scopes.begin() + static_cast<std::ptrdiff_t>(depth), m_Scopes.end());
		m_Scopes.resize(depth);
		return above;
	}

	void MirBuilder::Reattach(std::vector<Scope>&& scopes)
	{
		for (Scope& scope : scopes) m_Scopes.push_back(scope);
	}

	// --- unwinding ----------------------------------------------------------------------------------------------

	MirStepId MirBuilder::UnwindTarget()
	{
		return m_Scopes.size() > m_Floor ? StepOf(m_Scopes.size() - 1) : c_MirNone;
	}

	MirStepId MirBuilder::StepOf(size_t index)
	{
		if (m_Scopes[index].step != c_MirNone) return m_Scopes[index].step;
		const Scope scope = m_Scopes[index];
		MirStep step;
		step.next = index > scope.floor ? StepOf(index - 1) : c_MirNone;
		step.parent = scope.funclet;
		step.at = m_At;
		switch (scope.kind)
		{
		case Scope::Kind::Cleanup: step.kind = MirStep::Kind::Cleanup; break;
		case Scope::Kind::Abandon: step.kind = MirStep::Kind::Abandon; step.once = scope.once; break;
		case Scope::Kind::Handler: step.kind = MirStep::Kind::EndCatch; step.handler = scope.handler; break;
		case Scope::Kind::Try: break; // made with its scope
		}
		const MirStepId id = static_cast<MirStepId>(m_Body.steps.size());
		m_Body.steps.push_back(step);
		m_Scopes[index].step = id;
		if (scope.kind == Scope::Kind::Cleanup) BuildUnwindCopy(index, id);
		return id;
	}

	// The exception's copy of a cleanup: its body with only the scopes outside it, as code apart from the function's
	// own (a funclet). Below it nothing is a target: what its own code raises leaves the function.
	void MirBuilder::BuildUnwindCopy(size_t index, MirStepId step)
	{
		const Place outer = Save();
		const DeferNode* defer = m_Scopes[index].defer;
		std::vector<Scope> above = DetachFrom(index);
		const MirFuncletId funclet = NewFunclet(MirFunclet::Kind::Cleanup, step);
		m_Body.steps[step].funclet = funclet;
		m_Floor = index;
		m_FloorFunclet = funclet;
		m_CleanupOf = nullptr;
		const MirBlockId entry = NewBlock();
		m_Body.steps[step].body = entry;
		Enter(entry);
		MirTerminator unwind;
		unwind.kind = MirTerminator::Kind::Unwind;
		unwind.step = m_Body.steps[step].next;
		BuildCleanupCopy(defer, &unwind);
		Reattach(std::move(above));
		Restore(outer);
	}

	MirFuncletId MirBuilder::CurrentFunclet() const
	{
		for (size_t i = m_Scopes.size(); i-- > m_Floor; )
			if (m_Scopes[i].kind == Scope::Kind::Handler) return m_Body.handlers[m_Scopes[i].handler].funclet;
		return m_FloorFunclet;
	}

	uint32 MirBuilder::InnermostHandler() const
	{
		for (size_t i = m_Scopes.size(); i-- > m_Floor; )
			if (m_Scopes[i].kind == Scope::Kind::Handler) return m_Scopes[i].handler;
		return c_MirNone;
	}

	MirFuncletId MirBuilder::NewFunclet(MirFunclet::Kind kind, uint32 owner)
	{
		m_Body.funclets.push_back({ kind, owner });
		return static_cast<MirFuncletId>(m_Body.funclets.size() - 1);
	}

	// --- a return's way out -------------------------------------------------------------------------------------

	bool MirBuilder::RunsOnReturn(const Scope& scope)
	{
		return (scope.kind == Scope::Kind::Cleanup && !scope.defer->exceptionalOnly) || scope.kind == Scope::Kind::Handler;
	}

	MirBlockId MirBuilder::ReturnPathFrom(size_t depth)
	{
		for (size_t i = depth; i-- > 0; )
			if (RunsOnReturn(m_Scopes[i])) return ReturnPathOf(i);
		return FinalReturn();
	}

	// Scope `index`'s exit with only the scopes outside it, then on to the next: built once for every return that
	// crosses it.
	MirBlockId MirBuilder::ReturnPathOf(size_t index)
	{
		if (m_Scopes[index].returnPath != c_MirNone) return m_Scopes[index].returnPath;
		const MirBlockId block = NewBlock();
		m_Scopes[index].returnPath = block;
		const Place outer = Save();
		std::vector<Scope> above = DetachFrom(index);
		Enter(block);
		ExitScope(above.front());
		if (IsOpen()) Goto(ReturnPathFrom(index));
		Reattach(std::move(above));
		Restore(outer);
		return block;
	}

	MirBlockId MirBuilder::FinalReturn()
	{
		if (m_FinalReturn != c_MirNone) return m_FinalReturn;
		m_FinalReturn = NewBlock();
		const Place outer = Save();
		std::vector<Scope> all = DetachFrom(0);
		Enter(m_FinalReturn);
		End({ MirTerminator::Kind::ReturnStaged });
		Reattach(std::move(all));
		Restore(outer);
		return m_FinalReturn;
	}

	// --- exceptions ---------------------------------------------------------------------------------------------
	// A try is a dispatch step every block of its body unwinds to; each clause is a handler whose code runs apart (a
	// funclet) and is left by its catch's end. What no clause takes goes on to whatever the try itself unwinds to.

	void MirBuilder::BuildTry(const TryNode* node)
	{
		if (node->catches.empty())
		{
			BuildBlock(node->tryBlock);
			return;
		}
		const MirBlockId join = NewBlock();
		MirStep dispatch;
		dispatch.kind = MirStep::Kind::Dispatch;
		dispatch.next = UnwindTarget();
		dispatch.parent = CurrentFunclet();
		dispatch.at = m_At;
		dispatch.firstHandler = static_cast<uint32>(m_Body.handlers.size());
		dispatch.handlerCount = static_cast<uint32>(node->catches.size());
		const MirStepId step = static_cast<MirStepId>(m_Body.steps.size());
		m_Body.steps.push_back(dispatch);
		for (const TryNode::CatchClause& clause : node->catches)
		{
			const uint32 id = static_cast<uint32>(m_Body.handlers.size());
			MirHandler handler;
			handler.clause = &clause;
			handler.entry = NewBlock();
			handler.dispatch = step;
			handler.funclet = NewFunclet(MirFunclet::Kind::Handler, id);
			handler.at = clause.origin.line != 0 ? clause.origin : m_At;
			m_Body.blocks[handler.entry].handler = id;
			m_Body.handlers.push_back(handler);
		}

		const size_t tryDepth = m_Scopes.size();
		Scope tryScope;
		tryScope.kind = Scope::Kind::Try;
		tryScope.step = step;
		PushScope(tryScope);
		// The body starts in a block of its own that may raise: its handlers count as reached whenever the try is.
		const MirBlockId body = NewBlock();
		if (IsOpen()) Goto(body);
		Enter(body);
		MatchScopes();
		BuildBlock(node->tryBlock);
		if (IsOpen()) Goto(join);
		PopTo(tryDepth);

		for (uint32 i = 0; i < m_Body.steps[step].handlerCount; ++i) BuildHandler(m_Body.steps[step].firstHandler + i, join);
		Enter(join);
	}

	void MirBuilder::BuildHandler(uint32 handler, MirBlockId join)
	{
		const MirHandler entry = m_Body.handlers[handler];
		m_At = entry.at; // the handler's entry is the catch's line, not the try body's last
		const size_t depth = m_Scopes.size();
		Scope scope;
		scope.kind = Scope::Kind::Handler;
		scope.handler = handler;
		PushScope(scope);
		Enter(entry.entry);
		BuildBlock(entry.clause->block);
		if (IsOpen())
		{
			MirTerminator leave;
			leave.kind = MirTerminator::Kind::LeaveHandler;
			leave.handler = handler;
			leave.target = join;
			End(leave);
		}
		PopTo(depth);
	}

	void MirBuilder::BuildThrow(const ThrowNode* node)
	{
		const auto [first, count] = BuildRegions(node->value);
		MirTerminator raise;
		raise.kind = MirTerminator::Kind::Throw;
		raise.node = node;
		raise.handler = node->isRethrow ? InnermostHandler() : c_MirNone;
		raise.firstRegion = first;
		raise.regionCount = count;
		EndWith(raise);
		ContinueAfterTerminator();
	}

	// The body of a noexcept function runs inside a catch-all whose handler stops the program: an exception never
	// leaves the function.
	void MirBuilder::BuildNoexceptBody(const BlockNode* root)
	{
		MirStep dispatch;
		dispatch.kind = MirStep::Kind::Dispatch;
		dispatch.at = m_At;
		dispatch.firstHandler = static_cast<uint32>(m_Body.handlers.size());
		dispatch.handlerCount = 1;
		const MirStepId step = static_cast<MirStepId>(m_Body.steps.size());
		m_Body.steps.push_back(dispatch);
		const uint32 id = static_cast<uint32>(m_Body.handlers.size());
		MirHandler handler;
		handler.entry = NewBlock();
		handler.dispatch = step;
		handler.funclet = NewFunclet(MirFunclet::Kind::Handler, id);
		handler.at = m_At;
		m_Body.blocks[handler.entry].handler = id;
		m_Body.handlers.push_back(handler);
		{
			const Place outer = Save();
			Scope scope;
			scope.kind = Scope::Kind::Handler;
			scope.handler = id;
			PushScope(scope);
			Enter(handler.entry);
			End({ MirTerminator::Kind::Terminate });
			PopTo(0);
			Restore(outer);
		}

		Scope tryScope;
		tryScope.kind = Scope::Kind::Try;
		tryScope.step = step;
		PushScope(tryScope);
		const MirBlockId body = NewBlock();
		Goto(body);
		Enter(body);
		MatchScopes();
		BuildBlock(root);
		PopTo(0);
	}

	// A once region: the first pass runs it behind its guard (OnceBegin), and an exception that leaves it abandons the
	// guard (an Abandon step) so the next pass runs it again.
	void MirBuilder::BuildOnce(const OnceNode* node)
	{
		const MirBlockId run = NewBlock();
		const MirBlockId done = NewBlock();
		if (!IsOpen()) ContinueAfterTerminator();
		MirTerminator begin;
		begin.kind = MirTerminator::Kind::OnceBegin;
		begin.node = node;
		begin.target = run;
		begin.otherwise = done;
		End(begin);
		Enter(run);
		const size_t depth = m_Scopes.size();
		Scope abandon;
		abandon.kind = Scope::Kind::Abandon;
		abandon.once = node;
		PushScope(abandon);
		BuildBlock(node->body);
		EndScope(depth);
		if (IsOpen())
		{
			MirStatement end;
			end.kind = MirStatement::Kind::OnceEnd;
			end.node = node;
			Add(end);
			Goto(done);
		}
		Enter(done);
	}
} // namespace C4Compiler
