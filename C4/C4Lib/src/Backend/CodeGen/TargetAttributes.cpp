#include "Backend/CodeGen/TargetAttributes.h"
#include <llvm/IR/Module.h>

namespace Scripting
{
	void ApplyTarget(llvm::Module& module, const CpuTarget& target)
	{
		const std::string features = target.LlvmFeatures();
		for (llvm::Function& function : module)
		{
			if (function.isDeclaration()) continue;
			function.addFnAttr("target-cpu", target.Cpu());
			if (!features.empty()) function.addFnAttr("target-features", features);
		}
	}
} // namespace Scripting
