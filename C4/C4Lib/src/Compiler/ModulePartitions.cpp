#include "Compiler/ModulePartitions.h"
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/SplitModule.h>

namespace Scripting
{
	namespace
	{
		// No C or C++ symbol can contain a '.', so a foreign object never defines one of these names.
		constexpr const char* kLocalSuffix = ".local";

		void SuffixLocal(llvm::GlobalValue& value)
		{
			if (value.hasLocalLinkage()) value.setName(value.getName() + kLocalSuffix);
		}

		// SplitModule makes the local symbols global so that partitions reach each other's: renamed first, they cannot
		// meet a foreign object's symbol of the same name in the link.
		void SuffixLocalNames(llvm::Module& module)
		{
			for (llvm::Function& function : module) SuffixLocal(function);
			for (llvm::GlobalVariable& variable : module.globals()) SuffixLocal(variable);
			for (llvm::GlobalAlias& alias : module.aliases()) SuffixLocal(alias);
		}

		std::string ToBitcode(const llvm::Module& module)
		{
			std::string bitcode;
			llvm::raw_string_ostream out(bitcode);
			llvm::WriteBitcodeToFile(module, out);
			out.flush();
			return bitcode;
		}
	}

	std::vector<std::string> SplitModuleToBitcode(llvm::Module& module, unsigned count)
	{
		SuffixLocalNames(module);
		std::vector<std::string> partitions;
		llvm::SplitModule(module, count, [&](std::unique_ptr<llvm::Module> partition) {
			if (!partitions.empty()) partition->setModuleInlineAsm("");
			partitions.push_back(ToBitcode(*partition));
		});
		return partitions;
	}
}
