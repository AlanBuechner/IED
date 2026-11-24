#pragma once
#include <unordered_map>
#include <vector>

namespace llvm { class CallBase; class Function; class Type; class Value; }

namespace C4Compiler
{
	/// Whether a value of `type` may hold a heap object's address: a pointer, an integer as wide as one or wider, a
	/// vector or an aggregate holding one (the optimizer may carry a pointer in an integer: a copy, an alignment).
	bool MayHoldAddress(const llvm::Type* type);

	/// For each of `calls` (the function's statepoints to be): the values computed before the call and used after it that
	/// may hold a heap object's address (MayHoldAddress), in the order the function defines them. A constant, a stack
	/// slot's or a global's address and an integer widened from a narrower one hold none (Compiler/StackMaps lists the
	/// values in each statepoint's deopt operands; docs/compiler/04 "Stack maps").
	std::unordered_map<llvm::CallBase*, std::vector<llvm::Value*>> ValuesLiveAcross(llvm::Function& function, const std::vector<llvm::CallBase*>& calls);
}
