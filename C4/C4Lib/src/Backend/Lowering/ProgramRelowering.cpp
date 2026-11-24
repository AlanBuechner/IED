#include "Backend/Lowering/ProgramLowering.h"
#include "Model/ProgramModel.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

// A build server's body edit (docs/compiler/07 stage 2a): the edited bodies lowered again in the model the build before
// left, when that gives what a cold build of the edit lowers.
namespace C4Compiler
{
	namespace
	{
		uint64 CountOf(const std::map<std::string, uint64>& requests)
		{
			uint64 count = 0;
			for (const auto& [what, made] : requests) count += made;
			return count;
		}

		std::unordered_set<const void*> SetOf(const AskedAnswers& answers)
		{
			return { answers.begin(), answers.end() };
		}

		std::string NameOf(const FunctionRecord& record)
		{
			return std::string("'") + (record.name.empty() ? record.mangledName : record.name).c_str() + "'";
		}
	}

	std::string ProgramLowering::Relower(const std::vector<const FunctionRecord*>& records)
	{
		std::vector<size_t> units;
		if (std::string why = FindUnits(records, units); !why.empty()) return why;
		const uint64 requested = CountOf(m_Model.Making().Requests());
		const size_t violations = m_Lowering.Violations().size();
		std::vector<LoweredBody> bodies = m_Lowering.Lower(m_Model, records);
		if (CountOf(m_Model.Making().Requests()) != requested) return "an edited body asks the model for what it has not made";
		if (m_Lowering.Violations().size() != violations) return "lowering an edited body changed the model outside a request";
		std::vector<BodyNeeds> needs;
		std::unordered_map<size_t, const BodyNeeds*> swapped;
		for (size_t i = 0; i < units.size(); ++i)
			if (std::string why = Differs(bodies[i]); !why.empty()) return why;
		for (size_t i = 0; i < units.size(); ++i) needs.push_back(NeedsOf(*bodies[i].tree));
		for (size_t i = 0; i < units.size(); ++i) swapped.emplace(units[i], &needs[i]);
		if (ReplayFirstPass({}) != ReplayFirstPass(swapped)) return "an edited body makes the program need other functions or types first now";
		if (CountOf(m_Model.Making().Requests()) != requested) return "an edited body holds an object the model had not laid out the vtables of";
		if (std::string why = AsksDiffer(units, bodies); !why.empty()) return why;
		if (std::string why = FirstUsesDiffer(units); !why.empty()) return why;
		for (size_t i = 0; i < units.size(); ++i) Replace(units[i], std::move(bodies[i]), std::move(needs[i]));
		return {};
	}

	std::string ProgramLowering::FindUnits(const std::vector<const FunctionRecord*>& records, std::vector<size_t>& units) const
	{
		std::unordered_map<const FunctionRecord*, size_t> unitOf;
		for (size_t i = 0; i < m_Program.units.size(); ++i)
			if (m_Program.units[i].kind == UnitKind::Function) unitOf.emplace(m_Program.units[i].placedBy, i);
		for (const FunctionRecord* record : records)
		{
			const auto unit = unitOf.find(record);
			if (unit == unitOf.end()) return NameOf(*record) + " was not lowered by the build before";
			units.push_back(unit->second);
		}
		return {};
	}

	// An incomplete body or an error is reported as a cold build reports it.
	std::string ProgramLowering::Differs(const LoweredBody& after) const
	{
		if (!after.tree->complete || after.errors.HasErrors()) return NameOf(*after.record) + " has errors now: a cold build reports them";
		return {};
	}

	// Every function lowered first is needed before the first pass's bodies are kept (LowerWrittenBodies), so naming one
	// changes nothing; what a body is the first to need joins the next pass in that order, first named by it.
	ProgramLowering::FirstPassReach ProgramLowering::ReplayFirstPass(const std::unordered_map<size_t, const BodyNeeds*>& swapped)
	{
		FirstPassReach reach;
		std::unordered_set<const FunctionRecord*> needed;
		Core::Vector<const FunctionRecord*> records;
		m_Model.AllFunctionRecords(records);
		for (const FunctionRecord* record : records)
			if (LoweredFirst(*record)) needed.insert(record);
		std::unordered_set<const Type*> held;
		const auto need = [&](const FunctionRecord* record, const FunctionRecord* user) {
			if (record && needed.insert(record).second) reach.needed.push_back({ record, user });
		};
		for (size_t u = 0; u < m_Program.units.size(); ++u)
		{
			const LoweredUnit& unit = m_Program.units[u];
			if (unit.kind != UnitKind::Function || !LoweredFirst(*unit.placedBy)) continue;
			const auto swap = swapped.find(u);
			const BodyNeeds& needs = swap != swapped.end() ? *swap->second : unit.needs;
			for (const FunctionRecord* record : needs.functions) need(record, unit.placedBy);
			for (const Type* type : needs.objects)
				if (held.insert(type).second)
					for (const FunctionRecord* entry : m_Reach.VtableEntriesOf(type)) need(entry, unit.placedBy);
			for (const CensusNeed& census : needs.census)
				if (std::none_of(reach.census.begin(), reach.census.end(), [&census](const CensusNeed& known) { return known == census; }))
					reach.census.push_back(census);
		}
		return reach;
	}

	// What a cold build makes depends on what each pass asks: an answer only the new body asks for must have been made by
	// the old body's pass (it is then made where it was), and one only the old asked for must be made earlier, or asked
	// for in that pass by another lowering too (it is then made in that pass still).
	std::string ProgramLowering::AsksDiffer(const std::vector<size_t>& units, const std::vector<LoweredBody>& bodies) const
	{
		std::unordered_map<size_t, const AskedAnswers*> asksNow;
		for (size_t i = 0; i < units.size(); ++i) asksNow.emplace(units[i], &bodies[i].asks.answers);
		const auto askedByAnother = [&](const void* answer, uint32 phase, size_t self) {
			for (size_t u = 0; u < m_Program.units.size(); ++u)
			{
				const LoweredUnit& unit = m_Program.units[u];
				if (u == self || unit.asks.phase != phase) continue;
				const auto now = asksNow.find(u);
				const AskedAnswers& answers = now != asksNow.end() ? *now->second : unit.asks.answers;
				if (std::find(answers.begin(), answers.end(), answer) != answers.end()) return true;
			}
			return false;
		};
		const ModelMaking& making = m_Model.Making();
		for (size_t i = 0; i < units.size(); ++i)
		{
			const LoweringAsks& before = m_Program.units[units[i]].asks;
			const std::unordered_set<const void*> old = SetOf(before.answers);
			const std::unordered_set<const void*> now = SetOf(bodies[i].asks.answers);
			for (const void* answer : now)
				if (!old.count(answer) && making.MadeIn(answer) > before.phase)
					return NameOf(*bodies[i].record) + " asks for what the build before made after lowering it";
			for (const void* answer : old)
				if (!now.count(answer) && making.MadeIn(answer) == before.phase && !askedByAnother(answer, before.phase, units[i]))
					return NameOf(*bodies[i].record) + " no longer asks for what only it asked for";
		}
		return {};
	}

	// A body a swapped tree first named ends its diagnostics with where the program first uses it, which moves with the edit.
	std::string ProgramLowering::FirstUsesDiffer(const std::vector<size_t>& units) const
	{
		std::unordered_set<const HIRFunction*> swapped;
		for (size_t unit : units) swapped.insert(m_Program.units[unit].tree.get());
		for (const LoweredUnit& unit : m_Program.units)
			if (unit.kind == UnitKind::Function && unit.diagnosticCount && swapped.count(m_Reach.FirstUser(*unit.placedBy)))
				return NameOf(*unit.placedBy) + " has diagnostics that name where an edited body uses it";
		return {};
	}

	void ProgramLowering::Replace(size_t unit, LoweredBody after, BodyNeeds needs)
	{
		LoweredUnit& kept = m_Program.units[unit];
		m_Reach.NoteUse(*after.record, after.errors);
		Core::Vector<CompilationError>& diagnostics = m_Errors.GetErrorsMutable();
		Core::Vector<CompilationError> spliced;
		for (uint32 i = 0; i < kept.firstDiagnostic; ++i) spliced.Push(diagnostics[i]);
		for (const CompilationError& error : after.errors.GetErrors()) spliced.Push(error);
		for (uint32 i = kept.firstDiagnostic + kept.diagnosticCount; i < diagnostics.Count(); ++i) spliced.Push(diagnostics[i]);
		const uint32 count = after.errors.GetErrors().Count();
		// The bodies after it kept their diagnostics after its own (KeepBody).
		for (size_t later = unit + 1; later < m_Program.units.size(); ++later)
			if (LoweredUnit& other = m_Program.units[later]; other.kind == UnitKind::Function)
				other.firstDiagnostic = other.firstDiagnostic - kept.diagnosticCount + count;
		diagnostics = std::move(spliced);
		kept.diagnosticCount = count;
		m_Reach.FirstUsesMoved(kept.tree.get(), after.tree.get());
		kept.tree = std::move(after.tree);
		kept.asks = std::move(after.asks);
		kept.needs = std::move(needs);
	}
} // namespace C4Compiler
