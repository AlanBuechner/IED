#pragma once
#include "Backend/HIR/GlobalInit.h"
#include "Backend/HIR/HIRNodes.h"
#include "Backend/Lowering/BodyNeeds.h"
#include "Backend/Lowering/ParallelLowering.h"
#include "Backend/Lowering/TypeThunkPlan.h"
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace C4Compiler
{
	/// What a lowered tree is, which says where codegen emits it and what it makes of it.
	enum class UnitKind
	{
		Function,      // a function's body: written, made from a template or written by the compiler
		InvokeThunk,   // how reflection calls a function (D-50)
		TypeThunk,     // how reflection constructs, destroys or copies an object of a type (docs/runtime/02 section 2)
		ProcessStarts, // a round of `__c4_global_init`: the globals and static members that start before main
		ThreadStarts,  // a round of `__c4_thread_start`: the thread_locals a thread starts when it attaches
		FirstUseStart, // a script's thread_local, started on its first use on each thread (docs/runtime/04 section 2)
	};

	/// A complete tree for codegen to emit.
	struct LoweredUnit
	{
		UnitKind kind = UnitKind::Function;
		std::unique_ptr<HIRFunction> tree;
		const FunctionRecord* placedBy = nullptr; // whose partition it goes to (an own invoke thunk's: its target's)
		const SymbolInfo* variable = nullptr;     // FirstUseStart: the thread_local it starts
		std::vector<ConstantStart> constants;     // a round's globals that start as data where their storage can hold it
		LoweringAsks asks;                        // what its lowering asked the model for (a body or a thunk)
		BodyNeeds needs;                          // what it needs lowered with it (a function's body)
		uint32 firstDiagnostic = 0;               // a function's body: where its lowering's diagnostics are in the program's
		uint32 diagnosticCount = 0;
	};

	/// A census counter (CensusNeed) and its body; codegen gives each partition that allocates such objects its own.
	struct CensusFunction
	{
		CensusNeed need;
		const FunctionRecord* record = nullptr;
		std::unique_ptr<HIRFunction> tree;
	};

	/// A function reflection calls through `thunk`: its own, or the one its signature shares, which calls the code it is
	/// given (D-50).
	struct InvokeThunkUse
	{
		const FunctionRecord* target = nullptr;
		const FunctionRecord* thunk = nullptr;
		bool shared = false;
	};

	/// Everything ProgramLowering lowered for codegen to emit, in the order it was lowered, and what the program's
	/// storage and reflection need of it. Codegen decides nothing about what is lowered: it emits this.
	struct LoweredProgram
	{
		std::vector<LoweredUnit> units;
		std::vector<CensusFunction> census;
		std::vector<InvokeThunkUse> invokeThunks;
		TypeThunkPlan types;
		/// `--script-host`: the classes whose vtables the program has whether or not it makes one of them.
		std::vector<const ObjectType*> sharedVtables;
		/// The function each function-pointer global starts at (`funcptr<R(A)> g = F;`), as its initializer names it.
		std::unordered_map<const SymbolInfo*, const FunctionRecord*> functionPointerStarts;
		/// Every function whose body was lowered, completely or not.
		std::unordered_set<const FunctionRecord*> lowered;
		/// The records of what the compiler wrote that the model has none of (thunks, census counters).
		std::deque<FunctionRecord> records;
		/// The trees that did not lower completely, kept for the diagnostics that name a use in them.
		std::vector<std::unique_ptr<HIRFunction>> failed;

		const CensusFunction* CensusOf(const CensusNeed& need) const
		{
			for (const CensusFunction& function : census)
				if (function.need == need) return &function;
			return nullptr;
		}
	};
} // namespace C4Compiler
