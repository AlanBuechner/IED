#include "Backend/MIR/MirBody.h"
#include "Backend/MIR/MirBuilder.h"

namespace Scripting
{
	void DeleteMirBody(MirBody* body)
	{
		delete body;
	}

	void EnsureMir(HIRFunction& fn)
	{
		if (fn.complete && !fn.mir) fn.mir.reset(new MirBody(MirBuilder(fn, nullptr).Build()));
		for (const std::unique_ptr<HIRFunction>& nested : fn.nestedFunctions) EnsureMir(*nested);
	}
} // namespace Scripting
