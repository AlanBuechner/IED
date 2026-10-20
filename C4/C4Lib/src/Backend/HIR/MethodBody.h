#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	struct CodeBlockNode;

	/// A function body with the lifecycle work the compiler adds around it (docs/language/06): by-value
	/// parameters are destroyed when the function returns (the callee owns them); a create constructs the
	/// base and the fields before its body, a delete destroys them after it, a copy copies the base first -
	/// each unless the body's first statement calls the base's create / copy itself. A compiler-written
	/// lifecycle function has only those parts.
	QueryResult<void> LowerFunctionBody(LoweringContext& ctx, const FunctionRecord* record, const CodeBlockNode* body, BlockNode* root);
} // namespace Scripting
