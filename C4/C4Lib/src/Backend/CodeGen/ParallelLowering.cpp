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
		// Rounds of lowering at once a batch gets before the rest is lowered alone: a body with lambdas finds one more
		// closure made each round.
		constexpr unsigned kRounds = 16;

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
		std::vector<LoweredBody> bodies = LowerEach(model, records.size(),
			[&](size_t i, ErrorTracker& errors) { return LowerFunction(model, errors, records[i]); });
		for (size_t i = 0; i < records.size(); ++i) bodies[i].record = records[i];
		return bodies;
	}

	std::vector<LoweredBody> ParallelLowering::LowerEach(ProgramModel& model, size_t count, const LoweringJob& lower)
	{
		std::vector<LoweredBody> bodies(count);
		m_Stats.bodies += count;
		Prepare(model);
		const auto start = std::chrono::steady_clock::now();
		LowerInRounds(model, lower, bodies); // on one thread too, so the program is the same whatever the threads
		const auto alone = std::chrono::steady_clock::now();
		for (size_t i = 0; i < count; ++i)
			if (!bodies[i].tree)
			{
				bodies[i] = LowerAlone(lower, i);
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
		const auto start = std::chrono::steady_clock::now();
		m_PreparedTypes = LifecycleFacts::MakeCompilerWritten(model, m_PreparedTypes);
		model.Settle();
		m_Stats.prepareSeconds += SecondsSince(start);
	}

	// A round lowers every body still pending at once; a body that stopped for a change the change itself can make (a
	// template instance, a closure) has it made, in the batch's order, and is lowered again in the next round.
	void ParallelLowering::LowerInRounds(ProgramModel& model, const LoweringJob& lower, std::vector<LoweredBody>& bodies)
	{
		std::vector<size_t> pending(bodies.size());
		std::iota(pending.begin(), pending.end(), size_t(0));
		for (unsigned round = 0; round < kRounds && !pending.empty(); ++round)
		{
			std::vector<ModelFrozen> stops = LowerAtOnce(model, lower, pending, bodies);
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

	std::vector<ModelFrozen> ParallelLowering::LowerAtOnce(ProgramModel& model, const LoweringJob& lower, const std::vector<size_t>& pending,
		std::vector<LoweredBody>& bodies)
	{
		std::vector<ModelFrozen> stops(pending.size());
		model.Writes().Freeze(true);
		m_Pool->Run(pending.size(), [&](size_t k) {
			LoweredBody& body = bodies[pending[k]];
			body = {};
			try
			{
				body.tree = std::make_unique<HIRFunction>(lower(pending[k], body.errors));
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

	LoweredBody ParallelLowering::LowerAlone(const LoweringJob& lower, size_t i)
	{
		LoweredBody body;
		body.tree = std::make_unique<HIRFunction>(lower(i, body.errors));
		++m_Stats.attempts;
		return body;
	}
} // namespace Scripting
