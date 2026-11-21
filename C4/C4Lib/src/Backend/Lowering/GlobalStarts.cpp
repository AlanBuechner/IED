#include "Backend/Lowering/GlobalStarts.h"
#include "Model/ProgramModel.h"

namespace Scripting
{
	bool GlobalStarts::Lower()
	{
		if (!m_Checked) CheckConstantStarts(m_Model, m_Errors);
		m_Checked = true;
		bool lowered = false;
		for (bool more = true; more; lowered = lowered || more)
		{
			if (m_Script) MarkHostStorageStarted();
			const bool process = LowerRound(UnitKind::ProcessStarts);
			const bool thread = m_Script ? LowerStartsOnFirstUse() : LowerRound(UnitKind::ThreadStarts);
			more = process || thread;
		}
		return lowered;
	}

	bool GlobalStarts::LowerRound(UnitKind kind)
	{
		auto round = std::make_unique<HIRFunction>();
		std::vector<ConstantStart> constants;
		const bool started = kind == UnitKind::ProcessStarts ? LowerGlobalInit(m_Model, m_Errors, *round, m_Started, constants)
			: LowerThreadInit(m_Model, m_Errors, *round, m_Started, constants);
		if (!started) return false;
		round->complete = true;
		std::vector<LoweredUnit> unit;
		unit.push_back({ kind, std::move(round), nullptr, nullptr, std::move(constants) });
		m_Reach.Keep(std::move(unit));
		m_Reach.LowerNeeded();
		return true;
	}

	bool GlobalStarts::LowerStartsOnFirstUse()
	{
		std::vector<FirstUseStart> starts = LowerFirstUseStarts(m_Model, m_Errors, m_Started);
		std::vector<LoweredUnit> units;
		for (FirstUseStart& start : starts)
		{
			start.function->complete = true;
			units.push_back({ UnitKind::FirstUseStart, std::move(start.function), nullptr, start.variable, {} });
		}
		m_Reach.Keep(std::move(units));
		m_Reach.LowerNeeded();
		return !starts.empty();
	}

	void GlobalStarts::MarkHostStorageStarted()
	{
		m_Model.Symbols()->CollectGlobalVariables([&](const SymbolInfo& variable) {
			if (m_Script->IsHostStorage(variable)) m_Started.insert(&variable);
		});
	}
} // namespace Scripting
