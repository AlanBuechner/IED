#include "Backend/Lowering/ProgramLowering.h"
#include "Backend/HIR/InvokeThunk.h"
#include "Backend/Lowering/GlobalStorage.h"
#include "Model/ProgramModel.h"
#include <cstring>

namespace C4Compiler
{
	namespace
	{
		bool IsWritten(const FunctionRecord& record)
		{
			return record.templateOrigin == TemplateOrigin::None && record.synthesized == LifecycleOp::None;
		}
	}

	ProgramLowering::ProgramLowering(ProgramModel& model, LoweringOptions options)
		: m_Model(model), m_Options(std::move(options)), m_Lowering(m_Options.threads), m_Reach(model, m_Lowering, m_Program, m_Errors),
		m_Starts(model, m_Reach, m_Errors, m_Options.script ? &*m_Options.script : nullptr)
	{
		const ScriptLowering* script = m_Options.script ? &*m_Options.script : nullptr;
		m_Program.types.ForScript(script);
		m_Invokes.ForScript(script);
	}

	// Thunks and starts come after every body, so the template instances the bodies make get their thunks and start their
	// static data members too; what the starts reach gets its thunks after them, and the methods those thunks reach theirs.
	bool ProgramLowering::Run()
	{
		auto step = std::chrono::steady_clock::now();
		LowerWrittenBodies();
		Timed("bodies", step);
		if (m_Options.everyVtable) HoldEveryObjectType();
		Timed("vtables", step);
		LowerThunks();
		Timed("thunks", step);
		for (bool more = true; more; )
		{
			m_Starts.Lower();
			Timed("global starts", step);
			LowerThunks();
			LowerInvokeThunks();
			Timed("thunks", step);
			more = LowerGlobalStorage();
			Timed("globals", step);
		}
		ReportViolations();
		return !m_Errors.HasErrors();
	}

	// A change of the model outside a request while lowerings shared it is a race in the compiler, never the program's
	// fault: it fails the build, so a test finds it whether or not the race did harm this time.
	void ProgramLowering::ReportViolations()
	{
		for (const std::string& violation : m_Lowering.Violations())
			m_Errors.AddError(SourceLocation(), Core::String("internal compiler error: lowering changed the model outside a request while "
				"lowerings shared it (") + violation.c_str() + ")");
	}

	// Where lowering starts: every function written as it is that has a body of its own (a nested one comes with its
	// parent); a template-made or compiler-written one is lowered when something needs it (D-16), and so is a script's
	// host's (its bodies are the host's: a script lowers the ones its code reaches, whatever of the interface it read).
	void ProgramLowering::LowerWrittenBodies()
	{
		Core::Vector<const FunctionRecord*> records;
		m_Model.AllFunctionRecords(records);
		for (const FunctionRecord* record : records)
			if (IsWritten(*record) && HasBody(*record) && !record->isLocal && !IsHosts(*record)) m_Reach.Need(record, nullptr);
		m_Reach.LowerNeeded();
	}

	bool ProgramLowering::IsHosts(const FunctionRecord& record) const
	{
		return m_Options.script && m_Options.script->IsHostUnit(record.definingUnit);
	}

	void ProgramLowering::HoldEveryObjectType()
	{
		for (const ObjectType* type : m_Model.ObjectTypes())
		{
			m_Program.sharedVtables.push_back(type);
			m_Reach.Holds(type, nullptr);
		}
		m_Reach.LowerNeeded();
	}

	void ProgramLowering::LowerThunks()
	{
		LowerInvokeThunks();
		LowerTypeThunks();
	}

	// Each round's thunks are lowered at once; a thunk that does not lower leaves its function one reflection cannot call
	// (its diagnostics are not the program's). Reflection names each target's code, so a target with no code yet (a
	// virtual method of an instance) is needed.
	void ProgramLowering::LowerInvokeThunks()
	{
		const auto defined = [this](const FunctionRecord& record) { return m_Reach.Defined(record); };
		while (true)
		{
			const std::vector<const FunctionRecord*> targets = m_Invokes.NewTargets(m_Model, defined);
			if (targets.empty()) return;
			const std::vector<InvokeThunkPlan::Job> jobs = m_Invokes.Plan(m_Model, targets, m_Lowering, m_Program.records);
			std::vector<LoweredBody> bodies = m_Lowering.LowerEach(m_Model, jobs.size(), [&](size_t i, ErrorTracker& errors) {
				return LowerInvokeThunk(m_Model, errors, *jobs[i].target, *jobs[i].thunk, jobs[i].shared);
			});
			std::vector<bool> complete(jobs.size());
			std::vector<LoweredUnit> thunks;
			for (size_t i = 0; i < jobs.size(); ++i)
			{
				complete[i] = bodies[i].tree->complete;
				const FunctionRecord* placedBy = jobs[i].shared ? jobs[i].thunk : jobs[i].target;
				if (complete[i]) thunks.push_back({ UnitKind::InvokeThunk, std::move(bodies[i].tree), placedBy });
			}
			m_Reach.Keep(std::move(thunks));
			for (const InvokeThunkUse& use : m_Invokes.Settle(jobs, complete))
			{
				m_Program.invokeThunks.push_back(use);
				m_Reach.Need(use.target, nullptr);
			}
			m_Reach.LowerNeeded();
		}
	}

	// A thunk that does not lower leaves its type without the operation.
	void ProgramLowering::LowerTypeThunks()
	{
		const std::vector<TypeThunkPlan::Job> jobs = m_Program.types.PlanNew(m_Model, m_Program.records);
		if (jobs.empty()) return;
		std::vector<LoweredBody> bodies = m_Lowering.LowerEach(m_Model, jobs.size(), [&](size_t i, ErrorTracker& errors) {
			return LowerTypeOperationThunk(m_Model, errors, *jobs[i].type, jobs[i].op, *jobs[i].thunk);
		});
		std::vector<LoweredUnit> thunks;
		for (size_t i = 0; i < jobs.size(); ++i)
		{
			if (!bodies[i].tree->complete)
			{
				m_Program.types.Failed(jobs[i]);
				continue;
			}
			m_Program.types.Lowered(jobs[i]);
			thunks.push_back({ UnitKind::TypeThunk, std::move(bodies[i].tree), jobs[i].thunk });
		}
		m_Reach.Keep(std::move(thunks));
		m_Reach.LowerNeeded();
	}

	// A global's storage starts as an image of its zeroed object with its vptrs set, or at the function a function
	// pointer names. A template instance made since has globals of its own: the caller looks again.
	bool ProgramLowering::LowerGlobalStorage()
	{
		for (const SymbolInfo* global : GlobalsWithStorage(m_Model))
		{
			if (!m_StorageSeen.insert(global).second) continue;
			if (const FunctionRecord* start = FunctionPointerStart(m_Model, *global))
			{
				m_Program.functionPointerStarts[global] = start;
				m_Reach.Need(start, nullptr);
			}
			else m_Reach.Holds(global->type, nullptr);
		}
		return m_Reach.LowerNeeded();
	}

	void ProgramLowering::Timed(const char* step, std::chrono::steady_clock::time_point& since)
	{
		const auto now = std::chrono::steady_clock::now();
		const double seconds = std::chrono::duration<double>(now - since).count();
		since = now;
		for (auto& [name, total] : m_StepSeconds)
			if (std::strcmp(name, step) == 0)
			{
				total += seconds;
				return;
			}
		m_StepSeconds.push_back({ step, seconds });
	}
} // namespace C4Compiler
