#pragma once
#include "Core/Core.h"
#include "Parser/AstNode.h"
#include <optional>
#include <string>
#include <unordered_set>

namespace Scripting
{
	/// A file's interface text (docs/runtime/04 section 3.2): `text`, which the file's top-level declarations
	/// `nodes` were parsed from (its text after the preprocessor and the interop pass), with the body of every
	/// function that is not part of a template removed. A definition becomes a declaration; an out-of-line
	/// definition (`void Point::Move() { ... }`), or one in `declaredElsewhere` (a function a header declares),
	/// disappears, its declaration staying: two declarations would move the function's visibility to the second.
	/// Template bodies and everything outside function bodies stay, and every line stays where it was. Empty when a
	/// node's range does not fit the text (the nodes were not parsed from it).
	std::optional<std::string> InterfaceText(const std::string& text, const Core::Vector<Ref<AstNode>>& nodes,
		const std::unordered_set<const AstNode*>& declaredElsewhere = {});
} // namespace Scripting
