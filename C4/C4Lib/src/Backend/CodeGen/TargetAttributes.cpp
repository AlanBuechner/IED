#include "Backend/CodeGen/TargetAttributes.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Model/ProgramModel.h"
#include "Model/TargetAnnotation.h"
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

	void ApplyFunctionTargets(ModuleContext& mctx, const ProgramModel& model)
	{
		Core::Vector<const FunctionRecord*> records;
		model.AllFunctionRecords(records);
		for (const FunctionRecord* rec : records)
		{
			const std::vector<std::string> features = TargetFeaturesOf(*rec);
			llvm::Function* function = features.empty() ? nullptr : mctx.FunctionIfDeclared(*rec);
			std::string error;
			const std::optional<CpuTarget> target = function && !function->isDeclaration() ? model.Target().With(features, error) : std::nullopt;
			if (target) function->addFnAttr("target-features", target->LlvmFeatures());
		}
	}
} // namespace Scripting
