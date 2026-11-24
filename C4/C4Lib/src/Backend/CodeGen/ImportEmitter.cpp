#include "Backend/CodeGen/ImportEmitter.h"
#include "Backend/CodeGen/ImportPlanner.h"
#include <algorithm>
#include <unordered_map>

namespace C4Compiler
{
	namespace
	{
		using Copies = std::vector<std::vector<const FunctionRecord*>>;

		// The facts of each partition's functions, gathered on its thread (a partition not emitted again keeps those of the
		// build before); the copies planned from all of them. An emitted partition's facts are kept in `partial`.
		Copies PlanCopies(PartitionSet& partitions, bool onlyAlwaysInline, WorkerPool& pool, PartialEmission* partial)
		{
			std::vector<FactsByRecord> facts(partitions.Count());
			pool.Run(partitions.Count(), [&](size_t i) {
				const unsigned partition = static_cast<unsigned>(i);
				if (!partitions.Emits(partition))
				{
					facts[i] = (*partial->facts)[i].functions;
					return;
				}
				ModuleContext& emitted = partitions.Partition(partition);
				facts[i] = FactsOf(emitted.module(), emitted.RecordsOfFunctions());
			});
			FactsByRecord all;
			Copies own(partitions.Count());
			for (size_t i = 0; i < facts.size(); ++i)
			{
				if (partial && partitions.Emits(static_cast<unsigned>(i))) partial->made[i].functions = facts[i];
				for (auto& [record, fact] : facts[i])
				{
					own[i].push_back(record);
					all.emplace(record, std::move(fact));
				}
			}
			const auto home = [&partitions](const FunctionRecord& record) { return partitions.Of(record); };
			Copies copies(partitions.Count());
			pool.Run(partitions.Count(), [&](size_t i) { // each reads the facts and the placement only
				copies[i] = ImportsOf(static_cast<unsigned>(i), own[i], all, home, onlyAlwaysInline);
			});
			if (partial)
				for (size_t i = 0; i < copies.size(); ++i)
					if (partitions.Emits(static_cast<unsigned>(i))) partial->made[i].copies = copies[i];
			return copies;
		}

		// A partition not emitted again comes out otherwise when it would copy other functions now, or one whose tree changed.
		void FindStaleCopies(const PartitionSet& partitions, const Copies& copies, PartialEmission& partial)
		{
			for (unsigned i = 0; i < partitions.Count(); ++i)
			{
				if (partitions.Emits(i)) continue;
				const std::vector<const FunctionRecord*>& before = (*partial.facts)[i].copies;
				const bool changed = std::any_of(before.begin(), before.end(), [&](const FunctionRecord* copy) { return partial.changed.count(copy) != 0; });
				if (changed || before != copies[i]) partial.stale[i] = true;
			}
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
	void EmitImports(PartitionSet& partitions, const ProgramModel& model, const LoweredProgram& program, bool onlyAlwaysInline, WorkerPool& pool,
		PartialEmission* partial)
	{
		const Copies copies = PlanCopies(partitions, onlyAlwaysInline, pool, partial);
		if (partial) FindStaleCopies(partitions, copies, *partial);
		const std::unordered_map<const FunctionRecord*, const HIRFunction*> trees = TreesOfFunctions(program);
		std::vector<std::vector<const HIRFunction*>> imported(partitions.Count());
		for (size_t i = 0; i < copies.size(); ++i)
			for (const FunctionRecord* record : copies[i])
				if (const auto tree = trees.find(record); tree != trees.end()) imported[i].push_back(tree->second);
		partitions.EmitCopies(model, imported, pool);
	}
} // namespace C4Compiler
