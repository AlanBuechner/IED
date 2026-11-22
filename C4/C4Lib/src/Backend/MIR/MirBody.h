#pragma once
#include "Backend/HIR/HIRNodes.h"
#include <vector>

namespace C4Compiler
{
	// =====================================================================
	// MIR (docs/compiler/06-mir.md): one function's control-flow graph, built from its HIR tree by MirBuilder where
	// lowering runs, checked and elaborated there (MirChecks, MirMoves), and emitted by CodeGen's MirEmitter.
	//
	// A block is statements and one terminator. A statement's expression is an HIR tree evaluated in place (its calls
	// unwind along the block's unwind edge); a block an expression runs in place (a Sequence step, `new T[n]`'s
	// start) is a region of MIR blocks of its own, entered from the statement and ended by RegionEnd. Scope exits are
	// elaborated: every exit runs copies of the cleanups it leaves, and an exception's way out is a chain of unwind
	// steps (a cleanup's own copy, a once region's abandon, a try's dispatch, a handler's end).
	// =====================================================================
	using MirBlockId = uint32;
	using MirStepId = uint32;
	using MirFuncletId = uint32;
	constexpr uint32 c_MirNone = 0xFFFFFFFFu;

	struct MirStatement
	{
		enum class Kind : uint8
		{
			Eval,        // node: an expression run for its effects (a call, a store, ++)
			Declare,     // node: a LocalDeclNode - its slot starts zeroed, or holding its initializer
			Asm,         // node: an AsmNode
			AtExit,      // node: an AtExitNode
			SetFlag,     // slot (a drop flag) = flag
			StageReturn, // node: what a return hands back (null: nothing), computed before the cleanups it crosses
			OnceEnd,     // node: the OnceNode whose region completed
		};
		Kind kind = Kind::Eval;
		bool flag = false;
		uint32 slot = 0;
		const HIRNode* node = nullptr;
		uint32 firstRegion = 0; // the regions its expression runs, in mir.regions
		uint32 regionCount = 0;
		SourceLocation at;      // the line its instructions carry
	};

	struct MirTerminator
	{
		enum class Kind : uint8
		{
			Open,         // not ended yet (the builder's)
			Goto,         // to `target`
			Branch,       // node: a condition; `target` when it holds, else `otherwise`
			Switch,       // node: an integer; cases [firstCase, +caseCount) of mir.cases, else `otherwise`
			Return,       // node: the value (null: none), returned at once - nothing is left to clean up
			ReturnStaged, // the one way out of the returns that crossed cleanups: what StageReturn staged
			FallOff,      // the end of the body: a void function returns, `main` returns 0, any other cannot get here
			Throw,        // node: a ThrowNode; a rethrow re-raises the exception of handler `handler` (c_MirNone: none)
			Unreachable,
			LeaveHandler, // handler `handler`'s catch ends, then `target`
			Unwind,       // the end of an unwinding cleanup's copy: the exception goes on to step `step` (c_MirNone: the caller)
			OnceBegin,    // node: a OnceNode; `target` runs its region the first time, `otherwise` is after it
			RegionEnd,    // the end of a region: its expression goes on
			Terminate,    // a noexcept function's handler: the exception stops the program
		};
		Kind kind = Kind::Open;
		const HIRNode* node = nullptr;
		MirBlockId target = c_MirNone;
		MirBlockId otherwise = c_MirNone;
		uint32 firstCase = 0;
		uint32 caseCount = 0;
		uint32 handler = c_MirNone;
		MirStepId step = c_MirNone;
		uint32 firstRegion = 0; // the regions its condition or value runs
		uint32 regionCount = 0;
		SourceLocation at;
	};

	struct MirSwitchCase
	{
		int64 value = 0;
		MirBlockId target = c_MirNone;
	};

	/// A block an expression runs in place: `block` is the HIR the expression names, `entry` its first MIR block and
	/// `exit` the one that ends it (RegionEnd; c_MirNone when its end cannot be reached).
	struct MirRegion
	{
		const BlockNode* block = nullptr;
		MirBlockId entry = c_MirNone;
		MirBlockId exit = c_MirNone;
	};

	struct MirBlock
	{
		std::vector<MirStatement> statements;
		MirTerminator end;
		MirStepId unwind = c_MirNone;        // where an exception raised here goes (c_MirNone: the caller)
		MirFuncletId funclet = c_MirNone;    // the catch handler or unwinding cleanup it runs in
		const DeferNode* cleanupOf = nullptr; // a copy of this cleanup's body runs here (its uses are the cleanup's)
		uint32 handler = c_MirNone;          // the handler this block is the entry of
		bool mayUnwind = false;              // an exception may be raised here: it runs code, or begins a try
		bool inPlace = false;                // a region's entry: emitted where its expression is
	};

	/// One step of an exception's way out of the scopes it was raised in.
	struct MirStep
	{
		enum class Kind : uint8
		{
			Cleanup,  // a copy of a cleanup's body runs (blocks from `body`, ending in Unwind)
			Abandon,  // a once region (`once`) gives up its guard
			Dispatch, // a try's handlers [firstHandler, +handlerCount) take what they catch
			EndCatch, // handler `handler`'s catch ends
		};
		Kind kind = Kind::Cleanup;
		MirStepId next = c_MirNone;        // where the exception goes after this step (c_MirNone: the caller)
		MirFuncletId parent = c_MirNone;   // the funclet the step's scope is in
		MirFuncletId funclet = c_MirNone;  // Cleanup: the funclet its copy runs in
		MirBlockId body = c_MirNone;       // Cleanup
		const OnceNode* once = nullptr;    // Abandon
		uint32 firstHandler = 0;           // Dispatch
		uint32 handlerCount = 0;
		uint32 handler = c_MirNone;        // EndCatch
		SourceLocation at;
	};

	/// A catch clause's handler (clause null: the catch-all that stops a noexcept function).
	struct MirHandler
	{
		const TryNode::CatchClause* clause = nullptr;
		MirBlockId entry = c_MirNone;
		MirStepId dispatch = c_MirNone;
		MirFuncletId funclet = c_MirNone;
		SourceLocation at;
	};

	/// Code that runs apart from the function's own flow: a catch handler, or a cleanup's copy for an exception.
	struct MirFunclet
	{
		enum class Kind : uint8 { Handler, Cleanup };
		Kind kind = Kind::Handler;
		uint32 owner = 0; // the handler, or the Cleanup step
	};

	/// A case that may not run into the next one (D-39): an error when its end can be reached from its start.
	struct MirCaseCheck
	{
		MirBlockId start = c_MirNone;
		MirBlockId end = c_MirNone;
		SourceLocation at;
	};

	/// A copy of a cleanup MirMoves may guard with a drop flag (D-05): `guard` goes to `body` (the copy), whose end goes
	/// to `after`; a moved variable's guard goes there only while its flag is set.
	struct MirDropGuard
	{
		const DeferNode* cleanup = nullptr;
		MirBlockId guard = c_MirNone;
		MirBlockId body = c_MirNone;
		MirBlockId after = c_MirNone;
	};

	/// Where an HIR block's statements begin: before statement `statement` of block `at` (a variable that has no
	/// declaration - a parameter, a catch variable - begins there).
	struct MirBlockStart
	{
		const BlockNode* block = nullptr;
		MirBlockId at = c_MirNone;
		uint32 statement = 0;
	};

	struct MirBody
	{
		std::vector<MirBlock> blocks; // blocks[0] is the entry
		std::vector<MirSwitchCase> cases;
		std::vector<MirRegion> regions;
		std::vector<MirStep> steps;
		std::vector<MirHandler> handlers;
		std::vector<MirFunclet> funclets;
		std::vector<MirBlockId> labels;    // every label's block (a label counts as reached, as goto may name it)
		std::vector<MirCaseCheck> caseChecks;
		std::vector<MirDropGuard> dropGuards;
		std::vector<MirBlockStart> starts;
		std::vector<bool> declared;        // by slot: a Declare starts it (else it is zeroed at entry)
		MirBlockId fallOff = c_MirNone;    // the block that runs off the end of the body

		static constexpr MirBlockId c_Entry = 0;

		/// The blocks control goes to from `block` when nothing is raised, in order (a region's statement goes into its
		/// regions first, then on: RegionEnd has none).
		template <typename Visit> void ForEachSuccessor(const MirBlock& block, Visit&& visit) const
		{
			const MirTerminator& end = block.end;
			switch (end.kind)
			{
			case MirTerminator::Kind::Goto:
			case MirTerminator::Kind::LeaveHandler: visit(end.target); break;
			case MirTerminator::Kind::Branch:
			case MirTerminator::Kind::OnceBegin: visit(end.target); visit(end.otherwise); break;
			case MirTerminator::Kind::Switch:
				for (uint32 i = 0; i < end.caseCount; ++i) visit(cases[end.firstCase + i].target);
				visit(end.otherwise);
				break;
			default: break;
			}
		}
	};

	/// MirBody's owner in an HIRFunction (the HIR tree keeps its MIR).
	void DeleteMirBody(MirBody* body);

	/// Builds the MIR of a complete tree lowering made without LowerFunction (a thunk, a round of starts, a census
	/// counter), and of its nested functions, where it has none: every tree codegen emits has its MIR.
	void EnsureMir(HIRFunction& fn);
} // namespace C4Compiler
