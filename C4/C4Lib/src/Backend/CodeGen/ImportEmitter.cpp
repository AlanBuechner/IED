#include "Backend/CodeGen/ImportEmitter.h"
#include "Backend/CodeGen/ImportPlanner.h"
#include <unordered_map>

namespace C4Compiler
{
	namespace
	{
		using Copies = std::vector<std::vector<const FunctionRecord*>>;

		// The facts of each partition's functions, gathered on its thread; the copies planned from all of them.
		Copies PlanCopies(PartitionSet& partitions, bool onlyAlwaysInline, WorkerPool& pool)
		{
			std::vector<FactsByRecord> facts(partitions.Count());
			pool.Run(partitions.Count(), [&](size_t i) {
				ModuleContext& partition = partitions.Partition(static_cast<unsigned>(i));
				facts[i] = FactsOf(partition.module(), partition.RecordsOfFunctions());
			});
			FactsByRecord all;
			Copies own(partitions.Count());
			for (size_t i = 0; i < facts.size(); ++i)
				for (auto& [record, fact] : facts[i])
				{
					own[i].push_back(record);
					all.emplace(record, std::move(fact));
				}
			const auto home = [&partitions](const FunctionRecord& record) { return partitions.Of(record); };
			Copies copies(partitions.Count());
			pool.Run(partitions.Count(), [&](size_t i) { // each reads the facts and the placement only
				copies[i] = ImportsOf(static_cast<unsigned>(i), own[i], all, home, onlyAlwaysInline);
			});
			return copies;
		}

		std::unordered_map<const FunctionRecord*, const HIRFunction*> TreesOfFunctions(const LoweredProgram& program)
		{
			std::unordered_map<const FunctionRecord*, const HIRFunction*> trees;
			for (const LoweredUnit& unit : program.units)
				if (unit.kind == UnitKind::Function) trees.emplace(unit.tree->record, unit.tree.get());
			return trees;
		}
	}

	// A copy references only what its function's own partition emitted with it, except the census counters of the types
	// it makes, which each partition has of its own.
	void EmitImports(PartitionSet& partitions, const ProgramModel& model, const LoweredProgram& program, bool onlyAlwaysInline, WorkerPool& pool)
	{
		const Copies copies = PlanCopies(partitions, onlyAlwaysInline, pool);
		const std::unordered_map<const FunctionRecord*, const HIRFunction*> trees = TreesOfFunctions(program);
		std::vector<std::vector<const HIRFunction*>> imported(partitions.Count());
		for (size_t i = 0; i < copies.size(); ++i)
			for (const FunctionRecord* record : copies[i])
				if (const auto tree = trees.find(record); tree != trees.end()) imported[i].push_back(tree->second);
		partitions.EmitCopies(model, imported, pool);
	}
} // namespace C4Compiler
