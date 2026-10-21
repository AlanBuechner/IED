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

	/// Recovery after a token nothing takes: skips the rest of its statement or declaration - past the next `;` outside
	/// the braces the skip opened, or up to a `}` that closes the enclosing block - so the tokens after it report nothing
	/// (BUG-311).
	void SkipRestOfStatement(Lexer& lexer);
}
