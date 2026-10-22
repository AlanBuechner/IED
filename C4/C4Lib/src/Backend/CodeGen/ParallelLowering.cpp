#include "Backend/CodeGen/ParallelLowering.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Model/ProgramModel.h"
#include <algorithm>
#include <chrono>
#include <numeric>

namespace Scripting
{
	namespace
	{
		constexpr unsigned kRounds = 4; // rounds of lowering at once a batch gets before the rest is lowered alone

		double SecondsSince(std::chrono::steady_clock::time_point start)
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		}
	}

	ParallelLowering::ParallelLowering(unsigned threads) : m_Threads(std::max(threads, 1u)), m_Pool(std::make_unique<WorkerPool>(m_Threads))
	{
	}

	std::vector<LoweredBody> ParallelLowering::Lower(ProgramModel& model, const std::vector<const FunctionRecord*>& records)
	{
		std::vector<LoweredBody> bodies(records.size());
		m_Stats.bodies += records.size();
		Prepare(model);
		const auto start = std::chrono::steady_clock::now();
		LowerInRounds(model, records, bodies); // on one thread too, so the program is the same whatever the threads
		const auto alone = std::chrono::steady_clock::now();
		for (size_t i = 0; i < records.size(); ++i)
			if (!bodies[i].tree)
			{
				bodies[i] = LowerAlone(model, records[i]);
				++m_Stats.alone;
			}
		m_Stats.atOnceSeconds += std::chrono::duration<double>(alone - start).count();
		m_Stats.aloneSeconds += SecondsSince(alone);
		return bodies;
	}

	// Whatever the threads, so the program is the same: what lowering would make on first use, made first, and every
	// answer the model keeps lazily made.
	void ParallelLowering::Prepare(ProgramModel& model)
	{
		LifecycleFacts::MakeCompilerWritten(model);
		model.Settle();
	}

	// A round lowers every body still pending at once; a body that stopped for a change the change itself can make (a
	// template instance) has it made, in the batch's order, and is lowered again in the next round.
	void ParallelLowering::LowerInRounds(ProgramModel& model, const std::vector<const FunctionRecord*>& records, std::vector<LoweredBody>& bodies)
	{
		std::vector<size_t> pending(records.size());
		std::iota(pending.begin(), pending.end(), size_t(0));
		for (unsigned round = 0; round < kRounds && !pending.empty(); ++round)
		{
			std::vector<ModelFrozen> stops = LowerAtOnce(model, records, pending, bodies);
			std::vector<size_t> again;
			for (size_t k = 0; k < pending.size(); ++k)
			{
				if (bodies[pending[k]].tree) continue;
				++m_Stats.changes[stops[k].what];
				if (!stops[k].make) continue;
				stops[k].make();
				++m_Stats.made;
				again.push_back(pending[k]);
			}
			if (again.empty()) return;
			Prepare(model);
			pending = std::move(again);
		}
	}

	std::vector<ModelFrozen> ParallelLowering::LowerAtOnce(ProgramModel& model, const std::vector<const FunctionRecord*>& records,
		const std::vector<size_t>& pending, std::vector<LoweredBody>& bodies)
	{
		std::vector<ModelFrozen> stops(pending.size());
		model.Writes().Freeze(true);
		m_Pool->Run(pending.size(), [&](size_t k) {
			LoweredBody& body = bodies[pending[k]];
			body = {};
			body.record = records[pending[k]];
			try
			{
				body.tree = std::make_unique<HIRFunction>(LowerFunction(model, body.errors, body.record));
			}
			catch (ModelFrozen& frozen)
			{
				body = {};
				stops[k] = std::move(frozen);
			}
		});
		model.Writes().Freeze(false);
		++m_Stats.rounds;
		m_Stats.attempts += pending.size();
		return stops;
	}

	LoweredBody ParallelLowering::LowerAlone(ProgramModel& model, const FunctionRecord* record)
	{
		LoweredBody body;
		body.record = record;
		body.tree = std::make_unique<HIRFunction>(LowerFunction(model, body.errors, record));
		++m_Stats.attempts;
		return body;
	}
} // namespace Scripting
