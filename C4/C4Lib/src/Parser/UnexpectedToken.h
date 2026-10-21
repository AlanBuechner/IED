#pragma once
#include "Lexer/Lexer.h"
#include "Core/Core.h"

namespace Scripting
{
	/// What to say about a token no parser accepts: a keyword written where a declaration names something
	/// (`int32 asm = 1;`) says so; anything else is an unexpected token.
	Core::String UnexpectedTokenMessage(const Lexer& lexer);

	/// Whether the next token is a keyword naming a variable after its type: `Core::String alias = ...;`, `T* asm;`.
	bool KeywordNamesAVariable(const Lexer& lexer);

	/// "'asm' is a keyword: it cannot name a variable, parameter or function".
	Core::String KeywordNameMessage(const Token& name);
}
