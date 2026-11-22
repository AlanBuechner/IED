#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Compiler/ErrorTracker.h"
#include "Compiler/WorkerPool.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace C4Compiler
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
		uint64_t bodies = 0;        // bodies lowered, each once
		uint64_t passes = 0;        // times work ran on the threads sharing the model (a batch, its needs, its MIR)
		double lowerSeconds = 0;    // lowering bodies on the threads
		double otherSeconds = 0;    // the other work on the threads (needs, MIR, thunk plans)
		double prepareSeconds = 0;  // settling the model before each pass and putting what it made in order after
		std::map<std::string, uint64_t> requests; // the changes lowering asked the model for, by kind
	};

	/// Lowers a batch of function bodies on several threads in one pass, the same whatever the threads. Every lowering
	/// reads the model without a lock; what one needs the model to make - a template instance, a closure, a
	/// compiler-written lifecycle function, lifecycle facts - is a request, made once under the model's lock by the
	/// first that asks and found by the others when it ends (docs/compiler/02, "Changes while bodies lower"). When the
	/// pass ends, what its requests made is put in one order (ProgramModel::SharedLowering), so record order and symbol
	/// names do not follow which thread asked first.
	class ParallelLowering
	{
	public:
		/// `threads` counts the calling thread; with one, the pass runs on it.
		explicit ParallelLowering(unsigned threads);

		std::vector<LoweredBody> Lower(ProgramModel& model, const std::vector<const FunctionRecord*>& records);
		/// The same for lowerings of another kind (an invoke thunk): `lower(i, errors)` lowers the ith of `count`; the
		/// bodies have no record.
		std::vector<LoweredBody> LowerEach(ProgramModel& model, size_t count, const LoweringJob& lower);
		/// Other work on the threads that reads the model and may ask it for changes (what lowered trees need, their MIR):
		/// `work(i)` for each i below `count`, sharing the model as a pass of lowering does.
		void Share(const ProgramModel& model, size_t count, const std::function<void(size_t)>& work);

		const LoweringStats& Stats() const { return m_Stats; }
		/// The model's changes made outside a request, or to what other lowerings read, while they shared it: each a race,
		/// the compiler's bug (empty when there was none).
		const std::vector<std::string>& Violations() const { return m_Violations; }
		/// The threads lowering keeps, for the other work of the emit to run on.
		WorkerPool& Pool() { return *m_Pool; }

	private:
		/// `work` on the threads while they share the model; the seconds spent in it are added to `seconds`.
		void RunShared(const ProgramModel& model, size_t count, const std::function<void(size_t)>& work, double& seconds);

		std::unique_ptr<WorkerPool> m_Pool;
		LoweringStats m_Stats;
		std::vector<std::string> m_Violations;
	};
} // namespace C4Compiler
