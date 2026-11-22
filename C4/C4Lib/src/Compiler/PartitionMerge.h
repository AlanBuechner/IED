#pragma once
#include "Compiler/ObjectEmission.h"
#include <memory>
#include <string>
#include <vector>

namespace llvm
{
	class LLVMContext;
	class Module;
}

namespace C4Compiler
{
	/// A whole-program build's cross-partition step (R12): the program, emitted in partitions as every linked build is
	/// (D-81), becomes one module again before it is optimized, so the optimizer sees all of it at once, as it did when
	/// the program was emitted whole. The partitions' bitcode (PartitionBitcode, PartitionOptimization::Whole), in
	/// partition order, and the modules `beside` builds (the reflection image, a script host's table), in that order,
	/// are read into `context` and linked; then every local the emission made a hidden `.local` global so another
	/// partition could name it is local again, under the name the program gave it (ProgramName). Null, with `error`,
	/// when a partition cannot be read back or the link fails.
	std::unique_ptr<llvm::Module> MergePartitions(llvm::LLVMContext& context, const std::vector<std::string>& partitions,
		const std::vector<KeyedModule::Builder>& beside, std::string& error);
}
