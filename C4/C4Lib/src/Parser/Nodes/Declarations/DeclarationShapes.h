#pragma once
#include "Parser/ParserState.h"

namespace Scripting
{
	/// Lookaheads that tell the shapes of `T name(...)` apart; each leaves the lexer where it was, at the '('.

	/// The group's matching ')' is followed by '{': a function with a body.
	bool ParenGroupIsFollowedByBody(Lexer& lexer);

	/// The group can be a parameter list: empty, or parameters (each a type, a name, a default) separated by commas.
	/// When it cannot, `T name(args);` is a variable started with those arguments (docs/language/04 section 4.10).
	/// A group of bare names (`T v(a, b)`) can be both and reads as parameters.
	bool ParenGroupIsParameterList(ParserState& state);
} // namespace Scripting
