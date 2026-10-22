#include "Compiler/IrVerification.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace Scripting
{
	// The module is verified once (which checks every function too); only a failure goes through the functions one by
	// one, so the diagnostic names the culprit (a bare verifyModule reports the first error without saying where).
	std::optional<std::string> InvalidIr(const llvm::Module& module)
	{
		if (!llvm::verifyModule(module)) return std::nullopt;
		std::string error;
		llvm::raw_string_ostream out(error);
		for (const llvm::Function& function : module)
			if (llvm::verifyFunction(function, &out))
				return "IR verification failed in function '" + function.getName().str() + "': " + error;
		llvm::verifyModule(module, &out);
		return "IR verification failed: " + error;
	}
}
