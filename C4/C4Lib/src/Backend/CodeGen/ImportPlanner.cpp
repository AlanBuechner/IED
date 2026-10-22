#include "Backend/CodeGen/ImportPlanner.h"
#include "Model/SymbolTable.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Module.h>
#include <algorithm>
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		// The limits of PartitionImports (ThinLTO's): a direct callee's instructions, and what each level further allows.
		constexpr uint32 kImportInstructions = 100;
		constexpr uint32 kImportDecayPercent = 70;
		constexpr int kImportLevels = 3;

		uint32 InstructionsUpTo(const llvm::Function& function, uint32 limit)
		{
			uint32 count = 0;
			for (const llvm::BasicBlock& block : function)
			{
				count += static_cast<uint32>(block.size());
				if (count > limit) return limit + 1;
			}
			return count;
		}

		FunctionFacts Facts(const llvm::Function& function, const RecordOfFunction& recordOf)
		{
			FunctionFacts facts;
			facts.instructions = InstructionsUpTo(function, kImportInstructions);
			facts.importable = !function.isInterposable() && !function.hasFnAttribute(llvm::Attribute::NoInline) &&
				!function.hasFnAttribute(llvm::Attribute::OptimizeNone);
			facts.alwaysInline = function.hasFnAttribute(llvm::Attribute::AlwaysInline);
			facts.copiesNothing = function.hasMinSize();
			for (const llvm::BasicBlock& block : function)
				for (const llvm::Instruction& instruction : block)
					if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
						if (const auto* callee = llvm::dyn_cast<llvm::Function>(call->getCalledOperand()->stripPointerCasts()))
							if (const auto record = recordOf.find(callee); record != recordOf.end()) facts.callees.push_back(record->second);
			return facts;
		}

		bool Copied(const FunctionFacts& facts, uint32 limit, bool onlyAlwaysInline)
		{
			if (!facts.importable) return false;
			return facts.alwaysInline || (!onlyAlwaysInline && facts.instructions <= limit);
		}
	}

	FactsByRecord FactsOf(const llvm::Module& module, const RecordOfFunction& recordOf)
	{
		FactsByRecord facts;
		for (const llvm::Function& function : module)
			if (!function.isDeclaration() && !function.hasAvailableExternallyLinkage())
				if (const auto record = recordOf.find(&function); record != recordOf.end()) facts[record->second] = Facts(function, recordOf);
		return facts;
	}

	std::vector<const FunctionRecord*> ImportsOf(unsigned partition, const std::vector<const FunctionRecord*>& own, const FactsByRecord& facts,
		const std::function<unsigned(const FunctionRecord&)>& home, bool onlyAlwaysInline)
	{
		std::unordered_set<const FunctionRecord*> present(own.begin(), own.end());
		std::vector<const FunctionRecord*> imports;
		std::vector<const FunctionRecord*> level = own;
		uint32 limit = kImportInstructions;
		for (int depth = 0; depth < kImportLevels && !level.empty(); ++depth)
		{
			std::vector<const FunctionRecord*> next;
			for (const FunctionRecord* caller : level)
			{
				const auto callerFacts = facts.find(caller);
				if (callerFacts == facts.end() || callerFacts->second.copiesNothing) continue;
				for (const FunctionRecord* callee : callerFacts->second.callees)
				{
					const auto calleeFacts = facts.find(callee);
					if (calleeFacts == facts.end() || present.contains(callee) || home(*callee) == partition) continue;
					if (!Copied(calleeFacts->second, limit, onlyAlwaysInline)) continue;
					present.insert(callee);
					next.push_back(callee);
				}
			}
			imports.insert(imports.end(), next.begin(), next.end());
			level = std::move(next);
			limit = limit * kImportDecayPercent / 100;
		}
		std::sort(imports.begin(), imports.end(), [](const FunctionRecord* a, const FunctionRecord* b) { return a->mangledName < b->mangledName; });
		return imports;
	}
} // namespace Scripting
