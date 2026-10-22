#pragma once

namespace llvm
{
	class Module;
}

namespace Scripting
{
	/// LLVM writes a CodeView variable with any constant location as one S_CONSTANT for its whole function, dropping
	/// its other ranges: a variable with another location, or constants that disagree, loses its constant ones.
	void DropPartialConstantLocations(llvm::Module& module);
}
