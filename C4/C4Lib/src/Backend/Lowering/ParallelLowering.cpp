#include "Backend/Lowering/ParallelLowering.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Model/ProgramModel.h"
#include <algorithm>
#include <chrono>
#include <optional>

namespace C4Compiler
{
	namespace
	{
		double SecondsBetween(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
		{
			return std::chrono::duration<double>(to - from).count();
		}

		// The requests made since `before`, by kind, added to `into`.
		void AddRequests(const std::map<std::string, uint64>& before, const std::map<std::string, uint64>& after, std::map<std::string, uint64_t>& into)
		{
			for (const auto& [what, count] : after)
			{
				const auto earlier = before.find(what);
				const uint64 made = count - (earlier == before.end() ? 0 : earlier->second);
				if (made) into[what] += made;
			}
		}
	}

	ParallelLowering::ParallelLowering(unsigned threads) : m_Pool(std::make_unique<WorkerPool>(std::max(threads, 1u)))
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
		RunShared(model, count, [&](size_t i) { bodies[i].tree = std::make_unique<HIRFunction>(lower(i, bodies[i].errors)); }, m_Stats.lowerSeconds);
		return bodies;
	}

	void ParallelLowering::Share(const ProgramModel& model, size_t count, const std::function<void(size_t)>& work)
	{
		RunShared(model, count, work, m_Stats.otherSeconds);
	}

	// Whatever the threads, so the program is the same: one thread shares the model as several do.
	void ParallelLowering::RunShared(const ProgramModel& model, size_t count, const std::function<void(size_t)>& work, double& seconds)
	{
		if (count == 0) return;
		const auto start = std::chrono::steady_clock::now();
		const std::map<std::string, uint64> before = model.Making().Requests();
		std::chrono::steady_clock::time_point shared;
		std::chrono::steady_clock::time_point done;
		{
			const ProgramModel::SharedLowering sharing(model);
			shared = std::chrono::steady_clock::now();
			m_Pool->Run(count, work);
			done = std::chrono::steady_clock::now();
		}
		const auto ordered = std::chrono::steady_clock::now();
		seconds += SecondsBetween(shared, done);
		m_Stats.prepareSeconds += SecondsBetween(start, shared) + SecondsBetween(done, ordered);
		++m_Stats.passes;
		AddRequests(before, model.Making().Requests(), m_Stats.requests);
		for (std::string& violation : model.Making().TakeViolations())
			if (std::find(m_Violations.begin(), m_Violations.end(), violation) == m_Violations.end()) m_Violations.push_back(std::move(violation));
	}
} // namespace C4Compiler
