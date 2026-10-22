#include "Compiler/PartitionImports.h"
#include "Compiler/FunctionSize.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/InstrTypes.h>
#include <algorithm>
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		// Instructions of unoptimized IR a direct callee may have to be copied; each level deeper allows 70% of the one
		// before (ThinLTO's limit and decay).
		constexpr unsigned kImportInstructions = 100;
		constexpr unsigned kImportDecayPercent = 70;
		constexpr int kImportLevels = 3;

		bool Importable(const llvm::Function& function, unsigned limit)
		{
			if (function.isDeclaration() || function.isInterposable()) return false;
			if (function.hasFnAttribute(llvm::Attribute::NoInline) || function.hasFnAttribute(llvm::Attribute::OptimizeNone)) return false;
			return function.hasFnAttribute(llvm::Attribute::AlwaysInline) || HasAtMostInstructions(function, limit);
		}

		// A caller built for size inlines next to nothing, so it copies nothing (reflection's invoke thunks).
		std::vector<const llvm::Function*> Callees(const llvm::Function& function)
		{
			std::vector<const llvm::Function*> callees;
			if (function.hasMinSize()) return callees;
			for (const llvm::BasicBlock& block : function)
				for (const llvm::Instruction& instruction : block)
					if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
						if (const auto* callee = llvm::dyn_cast<llvm::Function>(call->getCalledOperand()->stripPointerCasts()))
							callees.push_back(callee);
			return callees;
		}

		std::vector<const llvm::Function*> Functions(const std::vector<const llvm::GlobalValue*>& members)
		{
			std::vector<const llvm::Function*> functions;
			for (const llvm::GlobalValue* member : members)
				if (const auto* function = llvm::dyn_cast<llvm::Function>(member)) functions.push_back(function);
			return functions;
		}
	}

	std::vector<const llvm::Function*> ImportsFor(const std::vector<const llvm::GlobalValue*>& members)
	{
		std::unordered_set<const llvm::Value*> present(members.begin(), members.end());
		std::vector<const llvm::Function*> imports;
		std::vector<const llvm::Function*> level = Functions(members);
		unsigned limit = kImportInstructions;
		for (int depth = 0; depth < kImportLevels && !level.empty(); ++depth)
		{
			std::vector<const llvm::Function*> next;
			for (const llvm::Function* caller : level)
				for (const llvm::Function* callee : Callees(*caller))
					if (!present.contains(callee) && Importable(*callee, limit))
					{
						present.insert(callee);
						next.push_back(callee);
					}
			imports.insert(imports.end(), next.begin(), next.end());
			level = std::move(next);
			limit = limit * kImportDecayPercent / 100;
		}
		std::sort(imports.begin(), imports.end(), [](const llvm::Function* a, const llvm::Function* b) { return a->getName() < b->getName(); });
		return imports;
	}
}
