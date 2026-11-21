#include "Backend/Lowering/ProgramReach.h"
#include "Backend/HIR/NativeBytes.h"
#include "Backend/Lowering/UseSites.h"
#include "Backend/MIR/MirBody.h"
#include "Model/ProgramModel.h"
#include "Model/Types/QualifiedName.h"

namespace Scripting
{
	namespace
	{
		// `int64(void* object)`, the census counters' signature.
		const FunctionType* CounterType(ProgramModel& model)
		{
			TypeStore* types = model.TypeStorePtr();
			return types->Function(types->Builtin(TypeKind::Int64), { types->Pointer(types->Builtin(TypeKind::Void)) });
		}

		std::string CounterSymbol(const CensusNeed& need)
		{
			return std::string("__c4_native_bytes.") + QualifiedName(need.type).c_str() + (need.countBytes ? "[]" : "");
		}
	}

	ProgramReach::ProgramReach(ProgramModel& model, ParallelLowering& lowering, LoweredProgram& program, ErrorTracker& errors)
		: m_Model(model), m_Lowering(lowering), m_Program(program), m_Errors(errors), m_Vtables(model)
	{
	}

	void ProgramReach::Need(const FunctionRecord* record, const HIRFunction* user)
	{
		if (!record || !m_Needed.insert(record).second) return;
		m_FirstUsers.emplace(record, user);
		m_Pending.push_back(record);
	}

	void ProgramReach::Holds(const Type* type, const HIRFunction* user)
	{
		if (!type || !m_Held.insert(type).second) return;
		for (const FunctionRecord* entry : m_Vtables.Of(type)) Need(entry, user);
	}

	void ProgramReach::Keep(std::vector<LoweredUnit> units)
	{
		// A thunk's or a round of starts' tree is made without LowerFunction: its MIR is built here, as codegen emits it.
		m_Lowering.Pool().Run(units.size(), [&](size_t i) { EnsureMir(*units[i].tree); });
		std::vector<const HIRFunction*> trees;
		for (const LoweredUnit& unit : units) trees.push_back(unit.tree.get());
		const std::vector<BodyNeeds> needs = NeedsOfEach(trees);
		for (size_t i = 0; i < units.size(); ++i)
		{
			NeedWhatItNeeds(*units[i].tree, needs[i]);
			m_Program.units.push_back(std::move(units[i]));
		}
	}

	bool ProgramReach::LowerNeeded()
	{
		bool lowered = false;
		while (!m_Pending.empty() || !m_PendingCensus.empty())
		{
			const std::vector<const FunctionRecord*> batch = TakeBatch();
			std::vector<LoweredBody> bodies = m_Lowering.Lower(m_Model, batch);
			std::vector<const HIRFunction*> trees;
			for (const LoweredBody& body : bodies) trees.push_back(body.tree.get());
			const std::vector<BodyNeeds> needs = NeedsOfEach(trees);
			for (size_t i = 0; i < bodies.size(); ++i) KeepBody(std::move(bodies[i]), needs[i]);
			LowerCensusNeeded();
			lowered = lowered || !batch.empty();
		}
		return lowered;
	}

	std::vector<BodyNeeds> ProgramReach::NeedsOfEach(const std::vector<const HIRFunction*>& trees)
	{
		std::vector<BodyNeeds> needs(trees.size());
		m_Lowering.Pool().Run(trees.size(), [&](size_t i) { needs[i] = NeedsOf(*trees[i]); });
		return needs;
	}

	std::vector<const FunctionRecord*> ProgramReach::TakeBatch()
	{
		std::vector<const FunctionRecord*> batch;
		for (const FunctionRecord* record : m_Pending)
			if (HasBody(*record) && !record->isLocal && m_Program.lowered.insert(record).second) batch.push_back(record);
		m_Pending.clear();
		return batch;
	}

	// An incomplete body is not emitted, but what it names is lowered too, so its callees' errors come with its own.
	void ProgramReach::KeepBody(LoweredBody body, const BodyNeeds& needs)
	{
		NoteUse(*body.record, body.errors);
		m_Errors.Append(body.errors);
		NeedWhatItNeeds(*body.tree, needs);
		if (!body.tree->complete)
		{
			m_Program.failed.push_back(std::move(body.tree));
			return;
		}
		m_Defined.insert(body.record);
		m_Program.units.push_back({ UnitKind::Function, std::move(body.tree), body.record });
	}

	void ProgramReach::NeedWhatItNeeds(const HIRFunction& user, const BodyNeeds& needs)
	{
		for (const FunctionRecord* record : needs.functions) Need(record, &user);
		for (const Type* type : needs.objects) Holds(type, &user);
		for (const CensusNeed& census : needs.census) NeedCensus(census, &user);
	}

	void ProgramReach::NeedCensus(const CensusNeed& need, const HIRFunction*)
	{
		if (m_Program.CensusOf(need)) return;
		for (const CensusNeed& pending : m_PendingCensus)
			if (pending == need) return;
		m_PendingCensus.push_back(need);
	}

	// Each counter is lowered once, and each partition that allocates such objects emits its own copy of it.
	void ProgramReach::LowerCensusNeeded()
	{
		for (size_t i = 0; i < m_PendingCensus.size(); ++i)
		{
			const CensusNeed need = m_PendingCensus[i];
			FunctionRecord& record = m_Program.records.emplace_back();
			record.type = CounterType(m_Model);
			record.mangledName = CounterSymbol(need).c_str();
			auto tree = std::make_unique<HIRFunction>(NativeBytes::LowerCensusFunction(m_Model, m_Errors, need.type, need.countBytes, record));
			EnsureMir(*tree);
			NeedWhatItNeeds(*tree, NeedsOf(*tree));
			m_Program.census.push_back({ need, &record, std::move(tree) });
		}
		m_PendingCensus.clear();
	}

	void ProgramReach::NoteUse(const FunctionRecord& record, ErrorTracker& errors) const
	{
		if (errors.GetErrors().Count() == 0) return;
		const auto user = m_FirstUsers.find(&record);
		const Core::String note = UseNote(m_Model, record, user == m_FirstUsers.end() ? nullptr : user->second);
		for (CompilationError& error : errors.GetErrorsMutable()) error.message += note;
	}
} // namespace Scripting
