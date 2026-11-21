#pragma once
#include "Backend/MIR/MirBody.h"
#include <unordered_set>
#include <vector>

namespace Scripting
{
	/// What MirMoves may elaborate later: the cleanups whose every copy is entered through a drop guard, and the blocks
	/// whose start is noted (where a variable without a declaration begins).
	struct MirBuildOptions
	{
		std::unordered_set<const DeferNode*> guarded;
		std::unordered_set<const BlockNode*> noted;
	};

	/// Builds a function's MIR from its complete HIR tree (docs/compiler/06-mir.md). Every block a body lowers into is a
	/// frame; every DeferNode a cleanup scope. Each exit edge - running off a block, break, continue, fallthrough, goto -
	/// runs copies of the cleanups it leaves (leaving crossed catch handlers); every return goes the one way out, where
	/// each scope's exit is built once (ReturnPathFrom); and the first block that may raise an exception inside a scope
	/// makes that scope's unwind step (StepOf), whose cleanup copy runs with only the scopes outside it.
	class MirBuilder
	{
	public:
		MirBuilder(const HIRFunction& hir, const MirBuildOptions* options);
		MirBody Build();

	private:
		struct Scope
		{
			enum class Kind : uint8 { Cleanup, Abandon, Try, Handler };
			Kind kind = Kind::Cleanup;
			const DeferNode* defer = nullptr;   // Cleanup
			const OnceNode* once = nullptr;     // Abandon
			uint32 handler = c_MirNone;         // Handler
			MirStepId step = c_MirNone;         // its unwind step (a Try's is made with it, the others' on first need)
			MirBlockId returnPath = c_MirNone;  // Cleanup, Handler: its exit on every return's way out, on first need
			MirFuncletId funclet = c_MirNone;   // the funclet it was entered in
			size_t floor = 0;                   // the unwind floor when it was entered
		};
		struct Frame { const BlockNode* block = nullptr; size_t statementDepth = 0; };
		struct Label { MirBlockId block = c_MirNone; size_t depth = 0; bool placed = false; const BlockNode* owner = nullptr; };
		/// Where building is: restored after a copy built out of order (a return's way out, an unwinding cleanup).
		struct Place
		{
			MirBlockId current = c_MirNone;
			SourceLocation at;
			const DeferNode* cleanupOf = nullptr;
			size_t floor = 0;
			MirFuncletId floorFunclet = c_MirNone;
		};

		// --- blocks (MirBuilder.cpp) ---------------------------------------------------------------------------
		MirBlockId NewBlock();
		/// Builds on in `block` from here, in the scopes as they are now.
		void Enter(MirBlockId block);
		MirBlock& Current() { return m_Body.blocks[m_Current]; }
		bool IsOpen() const { return m_Body.blocks[m_Current].end.kind == MirTerminator::Kind::Open; }
		void Add(MirStatement statement);
		void End(MirTerminator end);
		void Goto(MirBlockId target);
		/// A new block for what follows a terminator: unreachable, unless a label makes it live.
		void ContinueAfterTerminator();
		/// The current block runs its code in the current scopes (where an exception goes, the funclet, the cleanup it
		/// copies): it is, or what follows goes on in a block of its own.
		void MatchScopes();
		Place Save() const;
		void Restore(const Place& place);

		// --- statements (MirBuilder.cpp) -----------------------------------------------------------------------
		void BuildBlock(const BlockNode* block);
		void BuildStatement(const HIRNode* statement);
		void BuildDeclare(const LocalDeclNode* declaration);
		void BuildIf(const IfNode* node);
		void BuildWhile(const WhileNode* node);
		void BuildFor(const ForNode* node);
		void BuildForLoop(const ForNode* node);
		void BuildSwitch(const SwitchNode* node);
		void BuildCase(const BlockNode* block, const SourceLocation& fallCheck, MirBlockId next);
		void BuildReturn(const ReturnNode* node);
		void BuildGoto(const GotoNode* node);
		void BuildLabel(const LabelNode* node);
		void DeclareLabels(const BlockNode* block);
		void DefineLabel(uint32 id, MirBlockId block);
		/// Label `id` (lowering numbers a body's labels from 1, densely).
		Label& LabelOf(uint32 id);
		size_t ForwardJumpDepth(const BlockNode* owner) const;
		/// The blocks `expression` runs in place (a Sequence's block step, `new T[n]`'s start), each built as a region;
		/// returns where they are in m_Body.regions.
		std::pair<uint32, uint32> BuildRegions(const HIRNode* expression);
		void Evaluate(MirStatement::Kind kind, const HIRNode* node);
		/// A terminator whose node (a condition, a value) is evaluated first.
		void EndWith(MirTerminator end);

		// --- scopes, exits and unwinding (MirBuilderScopes.cpp) --------------------------------------------------
		void PushCleanup(const DeferNode* defer);
		void PushScope(Scope scope);
		void PopTo(size_t depth);
		void EndScope(size_t depth);
		void ExitCleanups(size_t depth);
		void ExitScope(const Scope& scope);
		void BranchThroughCleanups(size_t depth, MirBlockId target);
		/// A copy of a cleanup's body at the current point, entered through a drop guard when it may need one; `exit` ends
		/// the copy (Unwind for an exception's copy), or with no terminator the copy runs on into what follows.
		void BuildCleanupCopy(const DeferNode* defer, const MirTerminator* exit);
		std::vector<Scope> DetachFrom(size_t depth);
		void Reattach(std::vector<Scope>&& scopes);
		MirStepId UnwindTarget();
		MirStepId StepOf(size_t index);
		void BuildUnwindCopy(size_t index, MirStepId step);
		MirFuncletId CurrentFunclet() const;
		uint32 InnermostHandler() const;
		MirFuncletId NewFunclet(MirFunclet::Kind kind, uint32 owner);
		MirBlockId ReturnPathFrom(size_t depth);
		MirBlockId ReturnPathOf(size_t index);
		MirBlockId FinalReturn();
		static bool RunsOnReturn(const Scope& scope);

		// --- exceptions and once regions (MirBuilderScopes.cpp) ---------------------------------------------------
		void BuildTry(const TryNode* node);
		void BuildHandler(uint32 handler, MirBlockId join);
		void BuildThrow(const ThrowNode* node);
		void BuildNoexceptBody(const BlockNode* root);
		void BuildOnce(const OnceNode* node);

		const HIRFunction& m_Hir;
		const MirBuildOptions* m_Options;
		MirBody m_Body;
		MirBlockId m_Current = c_MirNone;
		SourceLocation m_At;
		const DeferNode* m_CleanupOf = nullptr;
		std::vector<Scope> m_Scopes;
		size_t m_Floor = 0;
		MirFuncletId m_FloorFunclet = c_MirNone;
		std::vector<Frame> m_Frames;
		std::vector<Label> m_Labels;
		MirBlockId m_FinalReturn = c_MirNone;
	};
} // namespace Scripting
