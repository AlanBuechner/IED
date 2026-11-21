#pragma once
#include "Backend/MIR/MirBody.h"
#include "Model/QueryResult.h"
#include <vector>

namespace Scripting
{
	/// Where control can go in a function's MIR (docs/compiler/06-mir.md "Checks").
	namespace MirReach
	{
		/// The blocks control can reach from `roots`: by the terminators, into the regions a block's code runs, and along
		/// an exception's way out of a block that may raise one (each unwind step, a dispatch to its handlers and, unless
		/// one takes everything, on).
		std::vector<bool> From(const MirBody& mir, const std::vector<MirBlockId>& roots);
		/// The blocks control may get to next from `block` (as From follows them), each once or more.
		void Successors(const MirBody& mir, const MirBlock& block, std::vector<MirBlockId>& out);
		/// The blocks an exception goes to from step `step` on (c_MirNone: none, it leaves the function).
		void StepTargets(const MirBody& mir, MirStepId step, std::vector<MirBlockId>& out);

		/// Whether control can run off the end of the body (D-55). A label counts as reached: a goto may name it.
		bool FallsOffEnd(const MirBody& mir);
		/// D-39: a case whose end can be reached from its start runs into the next one, an error at the case.
		QueryResult<void> CheckCases(const MirBody& mir);
	}
} // namespace Scripting
