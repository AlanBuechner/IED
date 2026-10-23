#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Compiler/ErrorTracker.h"
#include "Compiler/WorkerPool.h"
#include "Model/ModelWrites.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Scripting
{
	class ProgramModel;
	struct FunctionRecord;

	/// A function's lowered body and what its lowering reported.
	struct LoweredBody
	{
		const FunctionRecord* record = nullptr;
		std::unique_ptr<HIRFunction> tree;
		ErrorTracker errors;
	};

	/// Lowers the ith of a batch, with its diagnostics in the tracker given (LowerFunction for a function's body).
	using LoweringJob = std::function<HIRFunction(size_t, ErrorTracker&)>;

	/// How lowering on several threads went (-ftime).
	struct LoweringStats
	{
		uint64_t bodies = 0;    // functions lowered
		uint64_t attempts = 0;  // lowerings run, those thrown away included
		uint64_t alone = 0;     // lowered alone, after the rounds
		uint64_t rounds = 0;    // rounds of lowering at once
		uint64_t made = 0;      // changes made between rounds
		double atOnceSeconds = 0; // in rounds of lowering at once
		double aloneSeconds = 0;  // lowering bodies alone
		double prepareSeconds = 0; // making what lowering would make on first use, before each round
		std::map<std::string, uint64_t> changes; // what stopped bodies in a round, by kind
	};

	/// Lowers a batch of function bodies on several threads, the same whatever the threads. A round lowers the pending
	/// bodies at once against the frozen model (ModelWrites), each with its own diagnostics and scope cursor; a body
	/// whose lowering would change the model stops. A change that can be made apart from its lowering (a template
	/// instance) is made after the round, in the batch's order, and its body goes to the next round; after a few
	/// rounds, or for any other change (a closure, a local type), the bodies left are lowered alone, in the batch's
	/// order, making their changes. So a body never sees a change another body of its batch made unless it needed that
	/// change itself, and the changes are made in one order.
	class ParallelLowering
	{
	public:
		/// `threads` counts the calling thread; with one, the rounds run on it.
		explicit ParallelLowering(unsigned threads);

		std::vector<LoweredBody> Lower(ProgramModel& model, const std::vector<const FunctionRecord*>& records);
		/// The same for lowerings of another kind (an invoke thunk): `lower(i, errors)` lowers the ith of `count`; the
		/// bodies have no record.
		std::vector<LoweredBody> LowerEach(ProgramModel& model, size_t count, const LoweringJob& lower);
		const LoweringStats& Stats() const { return m_Stats; }
		/// The threads lowering keeps, for the other work of the emit to run on.
		WorkerPool& Pool() { return *m_Pool; }

	private:
		void Prepare(ProgramModel& model);
		void LowerInRounds(ProgramModel& model, const LoweringJob& lower, std::vector<LoweredBody>& bodies);
		/// Lowers the `pending` bodies at once; a body that would change the model is left without a tree, and what
		/// stopped it is at its place in the result.
		std::vector<ModelFrozen> LowerAtOnce(ProgramModel& model, const LoweringJob& lower, const std::vector<size_t>& pending,
			std::vector<LoweredBody>& bodies);
		LoweredBody LowerAlone(const LoweringJob& lower, size_t i);

		unsigned m_Threads;
		std::unique_ptr<WorkerPool> m_Pool;
		uint32 m_PreparedTypes = 0; // the model's object types whose compiler-written functions are made
		LoweringStats m_Stats;
	};
} // namespace Scripting
