#pragma once
#include "Core/Core.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace llvm
{
	class Function;
	class Module;
}

namespace Scripting
{
	struct FunctionRecord;

	/// What a function's own partition knows of it that another partition needs to decide whether to copy it.
	struct FunctionFacts
	{
		uint32 instructions = 0;                     // of its unoptimized IR, counted to one past the largest copied
		bool importable = false;                     // neither noinline nor optnone nor interposable
		bool alwaysInline = false;                   // declared `inline`: copied whatever its size
		bool copiesNothing = false;                  // built for size (minsize): its callees are not copied for it
		std::vector<const FunctionRecord*> callees;  // called directly, in its order
	};

	using RecordOfFunction = std::unordered_map<const llvm::Function*, const FunctionRecord*>;
	using FactsByRecord = std::unordered_map<const FunctionRecord*, FunctionFacts>;

	/// The facts of each function `module` defines whose record `recordOf` knows.
	FactsByRecord FactsOf(const llvm::Module& module, const RecordOfFunction& recordOf);

	/// The functions a partition of a split program (D-81) copies, as `available_externally`, from the others: the
	/// rule Compiler/PartitionImports applies to a split module (D-76) - a direct callee of at most 100 unoptimized
	/// instructions, 70% of that a level further, three levels; an `inline` one whatever its size; with
	/// `onlyAlwaysInline` (-O0) only those - over `facts`, `own` being the partition's functions and `home` where each
	/// function is defined. Sorted by name, so the order does not depend on where the calls are.
	std::vector<const FunctionRecord*> ImportsOf(unsigned partition, const std::vector<const FunctionRecord*>& own, const FactsByRecord& facts,
		const std::function<unsigned(const FunctionRecord&)>& home, bool onlyAlwaysInline);
} // namespace Scripting
