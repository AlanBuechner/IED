#pragma once
#include "Lexer/Lexer.h"
#include "Core/Core.h"

namespace Scripting
{
	/// What to say about a token no parser accepts: a keyword written where a declaration names something
	/// (`int32 asm = 1;`) says so; anything else is an unexpected token.
	Core::String UnexpectedTokenMessage(const Lexer& lexer);
}
