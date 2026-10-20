#pragma once
#include <vector>

namespace llvm
{
	class Function;
	class GlobalValue;
}

namespace Scripting
{
	/// What a partition optimized alone (an incremental build, D-76) copies from the other partitions: the small
	/// functions its code calls, then the small functions those call, a few levels deep with a shrinking size limit, as
	/// ThinLTO imports them; a caller built for size (minsize) copies nothing. The copies are available_externally, so
	/// they inline and are then dropped; the partition's bitcode, and with it its cached object, changes when one of
	/// them does. Sorted by name, so the order does not depend on where in the program the calls are.
	std::vector<const llvm::Function*> ImportsFor(const std::vector<const llvm::GlobalValue*>& members);
}
