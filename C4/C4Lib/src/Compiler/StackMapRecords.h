#pragma once
#include <unordered_map>
#include <vector>

namespace llvm { class CallBase; class Function; class Instruction; class Value; }

namespace C4Compiler
{
	/// What each call of a function lists: the values live across it (Compiler/StackMapValues), or its frame's slots that
	/// may hold a pointer there (Compiler/StackMapSlots: each slot, its word count, its masks).
	using CallLists = std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>>;

	/// Where a record names the values live across its call: in place (a register, a spill slot), but a pointer or 64-bit
	/// integer live across a call in a loop that does not use it, through a slot of its own; or every value through a
	/// slot of its own (an aggregate's lists its words and masks, as a frame slot's), for code generated at -O0.
	enum class RecordedValues { InPlace, InSlots };

	/// Gives each of `calls` its record (docs/compiler/04 "Stack maps"): a `deopt` operand bundle - the count of words of the
	/// values `values` names for it, those words (each a pointer or a 64-bit integer, where it is while the call runs), then
	/// stack slots, each with its word count and masks: `slots`', and the slots of values recorded through one (`where`),
	/// each stored once where its value is made. A word live across a call in a loop that does not use it goes through
	/// one: as a deopt operand there it would count as a use at every call of the loop, and the register allocator would
	/// keep it in registers around them instead of spilling it once before the loop. Code generation lowers a call with
	/// such a bundle as a statepoint, which relocates nothing.
	/// With every value in a slot (-O0), a call whose slots are all static and that has a RecordPlace is recorded instead
	/// by an `llvm.experimental.stackmap` after it (Compiler/StackMapFormat: c_CallRecordId), masks over the function's
	/// slot table (one stackmap, c_SlotTableId): fast instruction selection lowers a stackmap itself, but hands each
	/// statepoint to SelectionDAG, which made -O0's code generation half as slow again. StackMapPlacement then puts each
	/// record at its call's return address.
	void RecordCalls(llvm::Function& function, const std::vector<llvm::CallBase*>& calls, const CallLists& values, const CallLists& slots,
		RecordedValues where);

	/// Where the stackmap that records `call` after it goes (-O0): right after a call; at the start of an invoke's normal
	/// destination, when the invoke is its only way in (StackMapPlacement moves it back to the call). Null where there is
	/// no such place.
	llvm::Instruction* RecordPlace(llvm::CallBase& call);
}
