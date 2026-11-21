#include "Backend/CodeGen/PartitionSet.h"
#include "Backend/CodeGen/CensusTypes.h"
#include "Compiler/IrVerification.h"
#include "Model/ProgramModel.h"
#include "Model/Types/QualifiedName.h"

namespace Scripting
{
	void PartitionSet::SplitInto(unsigned count)
	{
		m_Others.clear();
		for (unsigned i = 1; i < count; ++i) m_Others.push_back(std::make_unique<PartitionEmitter>(m_First.System()));
	}

	std::vector<llvm::Module*> PartitionSet::Modules()
	{
		std::vector<llvm::Module*> modules;
		for (unsigned i = 0; i < Count(); ++i) modules.push_back(&Partition(i).module());
		return modules;
	}

	void PartitionSet::Prepare(const ProgramModel& model, const LoweredProgram& program, const ModuleSettings& settings, WorkerPool& pool)
	{
		if (Split()) m_Placement.emplace(model, Count());
		if (Split()) SettleLayouts(model);
		pool.Run(Count(), [&](size_t i) { PrepareOne(static_cast<unsigned>(i), model, program, settings); });
	}

	void PartitionSet::PrepareOne(unsigned i, const ProgramModel& model, const LoweredProgram& program, const ModuleSettings& settings)
	{
		ModuleContext& partition = Partition(i);
		partition.SetModel(&model);
		partition.UseFunctionPointerStarts(&program.functionPointerStarts);
		partition.census().Use(program);
		if (m_Placement) partition.SetPartition([this, i](const SymbolInfo& global) { return m_Placement->Of(global) == i; });
		if (!settings.strictAliasing) partition.aliasTags().Disable();
		if (settings.debugInfo) partition.EnableDebugInfo(model.SourcePaths());
		for (const ObjectType* object : model.ObjectTypes()) partition.FillStruct(object);
	}

	void PartitionSet::Emit(const ProgramModel& model, const std::vector<Placed>& trees, WorkerPool& pool)
	{
		std::vector<std::vector<const HIRFunction*>> own(Count());
		for (const Placed& placed : trees) own[placed.partition].push_back(placed.tree);
		if (Split()) SettleLayouts(model);
		pool.Run(Count(), [&](size_t i) {
			for (const HIRFunction* tree : own[i]) EmitterOf(static_cast<unsigned>(i)).Emit(*tree);
			DefineCensusCounters(static_cast<unsigned>(i));
		});
	}

	void PartitionSet::EmitCopies(const ProgramModel& model, const std::vector<std::vector<const HIRFunction*>>& copies, WorkerPool& pool)
	{
		SettleLayouts(model);
		pool.Run(Count(), [&](size_t i) {
			const unsigned partition = static_cast<unsigned>(i);
			for (const HIRFunction* tree : copies[i]) EmitterOf(partition).EmitImport(*tree);
			Partition(partition).TakeNewFunctions();
			DefineCensusCounters(partition);
		});
	}

	void PartitionSet::DefineCensusCounters(WorkerPool& pool)
	{
		pool.Run(Count(), [&](size_t i) { DefineCensusCounters(static_cast<unsigned>(i)); });
	}

	void PartitionSet::DefineCensusCounters(unsigned i)
	{
		for (const CensusFunction* counter : Partition(i).census().TakeFunctions()) EmitterOf(i).Emit(*counter->tree);
	}

	std::vector<std::string> PartitionSet::NamedButNotLowered(const LoweredProgram& program)
	{
		std::vector<std::string> missing;
		for (unsigned i = 0; i < Count(); ++i)
		{
			for (const FunctionRecord* record : Partition(i).TakeNewFunctions())
				if (HasBody(*record) && !record->isLocal && !program.lowered.count(record)) missing.push_back(record->mangledName.c_str());
			for (const Type* type : Partition(i).census().Unplanned()) missing.push_back(std::string("the census counter of ") + QualifiedName(type).c_str());
		}
		return missing;
	}

	void PartitionSet::DeclareOthersStaticStorage()
	{
		for (const std::unique_ptr<PartitionEmitter>& other : m_Others)
			for (const ModuleContext::StaticStorage& s : other->mctx().StaticStorages())
				m_First.DeclareStaticStorage(s.storage->getName().str(), s.type, s.storage->isThreadLocal());
	}

	std::optional<std::string> PartitionSet::FirstInvalid()
	{
		for (unsigned i = 0; i < Count(); ++i)
			if (std::optional<std::string> invalid = InvalidIr(Partition(i).module())) return invalid;
		return std::nullopt;
	}

	void PartitionSet::SettleLayouts(const ProgramModel& model) const
	{
		for (const ObjectType* type : model.ObjectTypes()) type->GetSize();
		for (const UnionType* type : model.UnionTypes()) type->GetSize();
	}
} // namespace Scripting
