#pragma once
#include <unordered_map>
#include <vector>

namespace llvm { class CallBase; class Function; class Value; }

namespace C4Compiler
{
	/// What each call of a function lists: the values live across it (Compiler/StackMapValues), or its frame's slots that
	/// may hold a pointer there (Compiler/StackMapSlots: each slot, its word count, its masks).
	using CallLists = std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>>;

	/// Gives each of `calls` its record (docs/compiler/04 "Stack maps"): a `deopt` operand bundle - the count of words of the
	/// values `values` names for it, those words (each a pointer or a 64-bit integer, where it is while the call runs), then
	/// stack slots, each with its word count and masks: `slots`', and a slot of its own for a value live across a call in a
	/// loop that does not use it, stored once where the value is made (as a deopt operand there it would count as a use at
	/// every call of the loop, and the register allocator would keep it in registers around them instead of spilling it
	/// once before the loop). Code generation lowers a call with such a bundle as a statepoint, which relocates nothing.
	void RecordCalls(llvm::Function& function, const std::vector<llvm::CallBase*>& calls, const CallLists& values, const CallLists& slots);
}
