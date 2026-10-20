#pragma once
#include "Backend/HIR/HIRNodes.h"

namespace Scripting::Reachability
{
	/// Whether control can reach the end of `block` (C++'s "can complete normally"): a return, throw, break,
	/// continue, goto or jump ends its block; a loop whose condition is always true ends only through a
	/// break that targets it; a label (a goto may reach it) makes the statements after it live again.
	bool CompletesNormally(const BlockNode& block);
}
