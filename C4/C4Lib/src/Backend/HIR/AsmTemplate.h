#pragma once
#include "Model/QueryResult.h"
#include <functional>
#include <string>
#include <vector>

namespace Scripting::AsmTemplate
{
	/// The LLVM inline-assembly template of an `asm` block's lines (docs/language/16): the lines joined by newlines,
	/// `{name}` and `{name:m}` (or `{0}`, `{1:m}` by position) replaced by `${index}` / `${index:m}` through
	/// `indexOf` (which returns -1 for a name that is not an operand), `{{` and `}}` by braces, and `$` doubled.
	/// Fails on an operand nobody declared, a modifier x86 does not have, or a brace left open.
	QueryResult<std::string> Translate(const std::vector<std::string>& lines, const std::function<int(const std::string&)>& indexOf);
}
