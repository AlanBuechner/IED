#pragma once
#include "Macro/MacroPass.h"
#include "SourceManager/SourceManager.h"
#include <unordered_map>

namespace Scripting
{
	/// The identity the macro pass knows a source by: its absolute path, or `memory:<id>` for one given as text.
	std::string MacroIdentity(const SourceInfo& info);

	/// Macro-expands every registered file of `sources` with the macros it starts with: `predefined`, then its
	/// project's defines, which replace one of the same name (D-59). An include carries the macros of the file the
	/// compiler would include. A file in `expanded` (an interface file, by id) is kept as it is and gives its includers
	/// the macros it maps to.
	std::vector<MacroPass::FileResult> ExpandSourceMacros(const SourceManager& sources, const std::map<std::string, std::string>& predefined,
		const std::unordered_map<uint32, MacroTable>& expanded = {});
}
