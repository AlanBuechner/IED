#include "Compiler/FunctionSize.h"
#include <llvm/IR/Function.h>

namespace Scripting
{
	bool HasAtMostInstructions(const llvm::Function& function, unsigned limit)
	{
		unsigned count = 0;
		for (const llvm::BasicBlock& block : function)
			for (auto at = block.begin(), end = block.end(); at != end; ++at)
				if (++count > limit) return false;
		return true;
	}
}
