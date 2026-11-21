#include "Backend/Lowering/InvokeThunkPlan.h"
#include "Backend/HIR/InvokeThunk.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Model/ProgramModel.h"
#include "Model/ReflectedEntities.h"
#include "Model/Types/VirtualTables.h"

namespace Scripting
{
	// Records are only added, and what makes one reflected and callable never changes: those are kept, in order, until
	// they have code to call.
	std::vector<const FunctionRecord*> InvokeThunkPlan::NewTargets(const ProgramModel& model, const std::function<bool(const FunctionRecord&)>& defined)
	{
		const Core::Vector<std::unique_ptr<FunctionRecord>>& records = model.Symbols()->Records();
		for (; m_RecordsScanned < records.Count(); ++m_RecordsScanned)
		{
			const FunctionRecord* record = records[m_RecordsScanned].get();
			const bool hosts = m_Script && m_Script->HostHas(record->mangledName.c_str());
			if (ReflectedFunction(*record) && Invokable(*record) && !hosts) m_Candidates.push_back(record);
		}
		std::vector<const FunctionRecord*> targets;
		std::vector<const FunctionRecord*> waiting;
		for (const FunctionRecord* record : m_Candidates)
		{
			if (!HasCallableCode(model, *record, defined)) waiting.push_back(record);
			else if (m_Planned.insert(record).second) targets.push_back(record);
		}
		m_Candidates = std::move(waiting);
		return targets;
	}

	std::vector<InvokeThunkPlan::Job> InvokeThunkPlan::Plan(ProgramModel& model, const std::vector<const FunctionRecord*>& targets, WorkerPool& pool,
		std::deque<FunctionRecord>& records)
	{
		m_Targets = targets;
		m_Wanted.assign(targets.size(), Wanted());
		pool.Run(targets.size(), [&](size_t i) {
			m_Wanted[i].shared = SharesInvokeThunk(model, *targets[i]);
			m_Wanted[i].symbol = m_Wanted[i].shared ? SharedInvokeThunkSymbol(*targets[i]) : InvokeThunkSymbol(*targets[i]);
		});
		std::vector<Job> jobs;
		std::unordered_set<std::string> planned;
		for (size_t i = 0; i < targets.size(); ++i)
		{
			const Wanted& wanted = m_Wanted[i];
			if (wanted.shared && (m_SharedThunks.count(wanted.symbol) || !planned.insert(wanted.symbol).second)) continue;
			FunctionRecord& thunk = records.emplace_back();
			thunk.type = InvokeThunkType(model);
			thunk.mangledName = wanted.symbol.c_str();
			jobs.push_back({ targets[i], &thunk, wanted.shared });
		}
		return jobs;
	}

	std::vector<InvokeThunkUse> InvokeThunkPlan::Settle(const std::vector<Job>& jobs, const std::vector<bool>& complete)
	{
		std::unordered_map<const FunctionRecord*, const FunctionRecord*> own; // a target's own thunk, lowered
		for (size_t i = 0; i < jobs.size(); ++i)
		{
			if (jobs[i].shared) m_SharedThunks[jobs[i].thunk->mangledName.c_str()] = complete[i] ? jobs[i].thunk : nullptr;
			else if (complete[i]) own[jobs[i].target] = jobs[i].thunk;
		}
		std::vector<InvokeThunkUse> uses;
		for (size_t i = 0; i < m_Targets.size(); ++i)
		{
			const auto shared = m_Wanted[i].shared ? m_SharedThunks.find(m_Wanted[i].symbol) : m_SharedThunks.end();
			const auto mine = own.find(m_Targets[i]);
			const FunctionRecord* thunk = shared != m_SharedThunks.end() ? shared->second : mine != own.end() ? mine->second : nullptr;
			if (thunk) uses.push_back({ m_Targets[i], thunk, m_Wanted[i].shared });
		}
		return uses;
	}

	// Every virtual method, body or not, is callable: its thunk dispatches through the object (docs/runtime/02 section 3.2).
	bool InvokeThunkPlan::HasCallableCode(const ProgramModel& model, const FunctionRecord& record, const std::function<bool(const FunctionRecord&)>& defined) const
	{
		if (defined(record)) return true;
		const ObjectType::MethodInfo* method = model.MethodForRecord(record);
		return method && VirtualTables::IsVirtual(record.containingType, *method);
	}
} // namespace Scripting
