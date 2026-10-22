#pragma once
#include "Lexer/Token.h"
#include <string>
#include <vector>

namespace Scripting
{
	struct FunctionRecord;

	/// `[target("avx2", "fma")]` on any declaration of `function` (docs/language/12): the processor features it is
	/// built for beyond the program's target, each once, in the order written. An argument that is not a string is
	/// an empty name, which no feature has.
	std::vector<std::string> TargetFeaturesOf(const FunctionRecord& function);

	/// Where the first `[target]` on the function's declarations stands.
	SourceLocation TargetAnnotationAt(const FunctionRecord& function);

	/// Whether the function returns a vector wider than an XMM register: Win64 returns one in YMM0 or ZMM0 only when
	/// the target has them, so a [target] function and its callers would disagree on where it is.
	bool ReturnsTargetDependentVector(const FunctionRecord& function);
}
