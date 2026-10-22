#pragma once
#include "Core/Core.h"
#include <string>
#include <vector>

namespace llvm
{
	class GlobalValue;
	class Module;
}

namespace Scripting
{
	/// The most instructions a host function may have (lowered, before optimization) for scripts to inline it.
	constexpr uint32 kInlineBodyLimit = 160;

	/// How many bodies make a group worth its own chunk.
	constexpr uint32 kInlineBodiesPerGroup = 100;

	/// The host functions a script may inline (docs/runtime/04 section 3.3), in groups of the module's order, one per
	/// kInlineBodiesPerGroup of them, at most `maxGroups`: each function `module` defines with at most kInlineBodyLimit
	/// instructions whose body names only `hostSymbols`, intrinsics and private constants, and no thread_local. A group
	/// lists the private constants its functions name (copied with them) before the functions. No group when no
	/// function qualifies.
	std::vector<std::vector<const llvm::GlobalValue*>> InlineBodyGroups(const llvm::Module& module,
		const std::vector<std::string>& hostSymbols, unsigned maxGroups);

	/// A group's module (ExtractDefinitions of it, from the program as Lower finished it) as the interface keeps it:
	/// its functions external, as the host table names them, without debug information, as bitcode. What a body names
	/// is declared under the same name.
	std::string InlineBodyBitcode(llvm::Module& group);

	/// Each of InlineBodyGroups' groups extracted from `module` and made bitcode, one after another: for a build that
	/// does not split the module while it extracts them.
	std::vector<std::string> InlineBodies(const llvm::Module& module, const std::vector<std::string>& hostSymbols, unsigned maxGroups = 1);
} // namespace Scripting
