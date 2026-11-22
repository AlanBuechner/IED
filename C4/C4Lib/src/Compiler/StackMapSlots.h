#pragma once
#include <unordered_map>
#include <vector>

namespace llvm { class CallBase; class Function; class Value; }

namespace Scripting
{
	/// What each of `calls` (the function's statepoints to be) lists in its deopt operands (Compiler/StackMaps): the stack
	/// slots of the frame that may hold managed pointers while the call runs - each slot, its count of words (with
	/// c_WholeSlot: read every one), then a 32-bit mask per 32 words of those that may hold one. A slot whose address goes
	/// nowhere but its own loads and stores is listed where it is live (a load of it may follow the call before a store
	/// over all of it); any other slot at every call. A call that lists none is not in the map.
	std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>> FrameSlotsAt(llvm::Function& function, const std::vector<llvm::CallBase*>& calls);

	/// The flag of a frame slot's word count that says every word is read (GC/GCStackMaps.c, GC_SLOT_WHOLE).
	constexpr unsigned c_WholeSlot = 0x80000000u;
}
