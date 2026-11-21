#pragma once
#include "Core/Core.h"
#include "Parser/AstNode.h"
#include "Macro/MacroLayout.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Scripting
{
	/// A file's interface text (docs/runtime/04 section 3.2): the file's text as its parse read it - `written` through its
	/// macro layout `macros` (null: none) - which its top-level declarations `nodes` were parsed from, with the body of
	/// every function that is not part of a template removed. A definition becomes a declaration; an out-of-line
	/// definition (`void Point::Move() { ... }`), or one in `declaredElsewhere` (a function a header declares),
	/// disappears, its declaration staying: two declarations would move the function's visibility to the second.
	/// Template bodies, the definitions in `compileTime` (a `[compile_time]` function's: a script's constant runs its
	/// body, D-91) and everything outside function bodies stay, and every line stays where it was. Empty when a node's
	/// range does not fit the text (the nodes were not parsed from it).
	std::optional<std::string> InterfaceText(std::string_view written, const MacroLayout* macros, const Core::Vector<Ref<AstNode>>& nodes,
		const std::unordered_set<const AstNode*>& declaredElsewhere = {}, const std::unordered_set<const AstNode*>& compileTime = {});
} // namespace Scripting
