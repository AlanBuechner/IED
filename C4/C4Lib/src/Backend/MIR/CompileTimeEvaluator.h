#pragma once
#include "Backend/MIR/MirInterpreter.h"
#include "Model/CompileTimeCalls.h"
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class ProgramModel;

	/// Compile-time evaluation for the program (R6, D-91): a function marked `[compile_time]` called where a constant is
	/// needed runs here, its MIR interpreted over the folded arguments. Each callee is lowered (HIR and MIR, as a build
	/// lowers it) and kept once it compiles; one that does not compile, or is not marked, cannot run. Constants fold on several
	/// lowering threads at once: each call has an interpreter of its own, and only the kept bodies are shared.
	/// While the model is still being built (a constant folding in pass 2), the types a body holds are completed before it
	/// runs, so it never reads a half-built type.
	class CompileTimeEvaluator : public ICompileTimeCalls
	{
	public:
		explicit CompileTimeEvaluator(ProgramModel& model, CtfeLimits limits = {}) : m_Model(model), m_Limits(limits) {}

		/// `callee` on `arguments`: its result as a constant (a number, a bool, a character), or why it cannot be one.
		QueryResult<ConstantValue> Call(const FunctionRecord& callee, const std::vector<ConstantValue>& arguments, const SourceLocation& at) override;
		/// The bytes of what `callee` returns - an array, an object (a global's table) - as the target lays them out.
		QueryResult<std::vector<uint8>> CallForData(const FunctionRecord& callee, const std::vector<ConstantValue>& arguments, const SourceLocation& at) override;

	private:
		QueryResult<CtfeBytes> Run(const FunctionRecord& callee, const std::vector<ConstantValue>& arguments, const SourceLocation& at);
		QueryResult<const HIRFunction*> BodyOf(const FunctionRecord& record);
		const HIRFunction* Kept(const FunctionRecord& record);
		/// Lowers `record` for evaluation - a body that compiled, or why it did not - unless this thread is lowering it
		/// already: a constant in a body that needs the body's own function cannot be had.
		QueryResult<std::unique_ptr<HIRFunction>> Lower(const FunctionRecord& record);
		/// While the model resolves: the types `record` takes and returns, made whole before its body lowers, and every
		/// type its body's slots hold, before it runs.
		QueryResult<void> CompleteSignature(const FunctionRecord& record);
		QueryResult<void> CompleteTypes(const HIRFunction& body);

		ProgramModel& m_Model;
		CtfeLimits m_Limits;
		std::mutex m_Lock; // over the two maps; never held while a body lowers or runs
		std::unordered_map<const FunctionRecord*, std::unique_ptr<HIRFunction>> m_Bodies;
		std::unordered_map<std::thread::id, std::vector<const FunctionRecord*>> m_Lowering;
	};
} // namespace Scripting
