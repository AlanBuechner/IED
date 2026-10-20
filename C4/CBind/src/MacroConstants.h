#pragma once
// The object-like macros of a bound header that are constants, as C4 constants (BUG-284): `#define WM_CLOSE 0x0010`
// binds as `const int32 WM_CLOSE = 16;`.
#include "clang/Lex/Preprocessor.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace CBind
{
	/// The macros worth trying as constants: object-like, not empty, written in a header (not the compiler's or the
	/// command line's), named as C4 allows (not a C4 keyword, not starting with '_'), and spelled only with literals,
	/// names, operators, parentheses and type keywords (casts) - nothing that could derail the parse that evaluates
	/// them. Sorted by name.
	std::vector<std::string> ConstantCandidates(const clang::Preprocessor& pp);

	/// C4 declarations of the candidates that are integer, floating-point or narrow string constants. Each is
	/// declared in a second parse of `header` with `clangArgs` (`static const __typeof__((NAME)) __cbind_NAME = NAME;`)
	/// and evaluated there; one that does not parse or is not such a constant is left out, and so is one whose name
	/// `taken` holds (a bound type, function, enumerator or alias).
	std::string ConstantDeclarations(const std::vector<std::string>& candidates, const std::unordered_set<std::string>& taken,
		const std::string& header, const std::vector<std::string>& clangArgs, bool cxx);
} // namespace CBind
