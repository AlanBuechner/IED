#pragma once

namespace llvm { class Function; }

namespace Scripting
{
	/// Whether `function` has at most `limit` instructions, counting no further than one past it: a block's size is a
	/// walk of its instructions, so a large function's whole count is not paid for at each question.
	bool HasAtMostInstructions(const llvm::Function& function, unsigned limit);
}
