#pragma once
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"
#include <vector>

namespace Scripting
{
	struct FunctionRecord;

	/// Compile-time evaluation as the model asks for it (D-91): a call of a `[compile_time]` function in a constant
	/// runs there. The driver that builds the model gives it one (Backend/MIR/CompileTimeEvaluator); a model without one
	/// folds no calls.
	class ICompileTimeCalls
	{
	public:
		virtual ~ICompileTimeCalls() = default;
		/// `callee` on folded `arguments`: its result as a constant (a number, a bool, a character), or why it is none.
		virtual QueryResult<ConstantValue> Call(const FunctionRecord& callee, const std::vector<ConstantValue>& arguments, const SourceLocation& at) = 0;
		/// The bytes of what `callee` returns - an array, an object - as the target lays them out.
		virtual QueryResult<std::vector<uint8>> CallForData(const FunctionRecord& callee, const std::vector<ConstantValue>& arguments, const SourceLocation& at) = 0;
	};
} // namespace Scripting
