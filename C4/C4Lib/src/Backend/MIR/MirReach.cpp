#include "Backend/MIR/MirReach.h"

namespace C4Compiler::MirReach
{
	namespace
	{
		bool TakesEverything(const MirHandler& handler)
		{
			return !handler.clause || handler.clause->catchAll;
		}

		void Regions(const MirBody& mir, uint32 first, uint32 count, std::vector<MirBlockId>& out)
		{
			for (uint32 i = 0; i < count; ++i) out.push_back(mir.regions[first + i].entry);
		}
	}

	void StepTargets(const MirBody& mir, MirStepId step, std::vector<MirBlockId>& out)
	{
		while (step != c_MirNone)
		{
			const MirStep& s = mir.steps[step];
			if (s.kind == MirStep::Kind::Cleanup)
			{
				out.push_back(s.body); // the copy ends in Unwind, which goes on from there
				return;
			}
			if (s.kind == MirStep::Kind::Dispatch)
			{
				bool caught = false;
				for (uint32 i = 0; i < s.handlerCount; ++i)
				{
					const MirHandler& handler = mir.handlers[s.firstHandler + i];
					out.push_back(handler.entry);
					caught = caught || TakesEverything(handler);
				}
				if (caught) return;
			}
			step = s.next;
		}
	}

	void Successors(const MirBody& mir, const MirBlock& block, std::vector<MirBlockId>& out)
	{
		mir.ForEachSuccessor(block, [&](MirBlockId next) { out.push_back(next); });
		for (const MirStatement& statement : block.statements) Regions(mir, statement.firstRegion, statement.regionCount, out);
		Regions(mir, block.end.firstRegion, block.end.regionCount, out);
		if (block.end.kind == MirTerminator::Kind::Unwind) StepTargets(mir, block.end.step, out);
		if (block.mayUnwind) StepTargets(mir, block.unwind, out);
	}

	std::vector<bool> From(const MirBody& mir, const std::vector<MirBlockId>& roots)
	{
		std::vector<bool> reached(mir.blocks.size(), false);
		std::vector<MirBlockId> work;
		for (MirBlockId root : roots)
			if (root != c_MirNone && !reached[root])
			{
				reached[root] = true;
				work.push_back(root);
			}
		std::vector<MirBlockId> next;
		while (!work.empty())
		{
			const MirBlockId block = work.back();
			work.pop_back();
			next.clear();
			Successors(mir, mir.blocks[block], next);
			for (MirBlockId successor : next)
				if (successor != c_MirNone && !reached[successor])
				{
					reached[successor] = true;
					work.push_back(successor);
				}
		}
		return reached;
	}

	bool FallsOffEnd(const MirBody& mir)
	{
		if (mir.fallOff == c_MirNone) return false;
		std::vector<MirBlockId> roots = mir.labels;
		roots.push_back(MirBody::c_Entry);
		return From(mir, roots)[mir.fallOff];
	}

	QueryResult<void> CheckCases(const MirBody& mir)
	{
		for (const MirCaseCheck& check : mir.caseChecks)
		{
			std::vector<MirBlockId> roots = mir.labels;
			roots.push_back(check.start);
			if (!From(mir, roots)[check.end]) continue;
			auto falls = QueryResult<void>::Fail(QueryError::NotAValue, "this case falls into the next one: end it with 'break;' or 'fallthrough;'");
			falls.loc = check.at;
			return falls;
		}
		return QueryResult<void>::Success();
	}
} // namespace C4Compiler::MirReach
