#pragma once
#include "Backend/MIR/MirBody.h"
#include <string>

namespace Scripting
{
	/// A function's MIR as text, a line a statement or terminator: its blocks (`bb3 (unwind s1, funclet f0):`), then its
	/// unwind steps and handlers. For tests and for reading what the builder made.
	std::string MirText(const MirBody& mir);
} // namespace Scripting
