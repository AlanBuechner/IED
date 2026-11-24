#pragma once
#include <llvm/Support/CodeGen.h>

namespace llvm { class Module; }

namespace C4Compiler
{
	/// Stack maps (R16 phase 3, `-fstack-maps`, docs/compiler/04 "Stack maps"): the collector reads a C4 frame's pointers
	/// from the places the stack map names for the call the frame waits in, instead of the whole frame.

	/// The GC strategy a function with statepoints names (`gc "c4"`): it relocates nothing, and its records are written
	/// in C4's format.
	constexpr const char* c_StackMapsGc = "c4";

	/// Marks `module` for stack maps: a module flag the passes below and the object writer read.
	void MarkForStackMaps(llvm::Module& module);
	bool UsesStackMaps(const llvm::Module& module);

	/// After the optimizer, every call of a marked module's functions gets a record in the module's stack map (in C4's
	/// format, Compiler/StackMapFormat): a deopt list naming the values the function computed before the call and uses
	/// after it that may hold an object's address (where they are while the call runs: a register, a spill slot), then the frame's stack
	/// slots that may hold one. Nothing is relocated (the collector never moves an object), so the code around the call is
	/// what it was. A call left without a record, whose frame is read whole: a call in a funclet, inline assembly, a
	/// variadic call (a statepoint loses what the convention does for one), one that never returns (a throw), one in tail
	/// position (it keeps its sibling-call optimization), one that passes an aggregate on the stack (that copy is in the
	/// caller's outgoing area, which no record names), and a call of the C library (code generation knows those by name,
	/// and they never collect). Code generated at `level` None records every value through a slot of its own (its register
	/// allocator cannot spill a statepoint's register operands, and runs out of registers at a call many values live
	/// across). Does nothing to a module not marked.
	void InsertStatepoints(llvm::Module& module, llvm::CodeGenOptLevel level);
}
