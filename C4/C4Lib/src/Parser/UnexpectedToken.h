#pragma once
#include "Lexer/Lexer.h"
#include "Core/Core.h"

namespace Scripting
{
	/// What to say about a token no parser accepts: a keyword written where a declaration names something
	/// (`int32 asm = 1;`) says so; anything else is an unexpected token.
	Core::String UnexpectedTokenMessage(const Lexer& lexer);

	/// A declaration from the next token that writes a call as a template argument (`Fixed<Square(3)> f;`), where it
	/// reads as a function signature: says to parenthesize it, `Fixed<(Square(3))>` (D-91). Empty for any other.
	Core::String UnparenthesizedCallMessage(const Lexer& lexer);

	/// Whether the next token is a keyword naming a variable after its type: `Core::String alias = ...;`, `T* asm;`.
	bool KeywordNamesAVariable(const Lexer& lexer);

	/// "'asm' is a keyword: it cannot name a variable, parameter or function".
	Core::String KeywordNameMessage(const Token& name);

	/// Recovery after a token nothing takes: skips the rest of its statement or declaration - past the next `;` outside
	/// the braces the skip opened, or up to a `}` that closes the enclosing block - so the tokens after it report nothing
	/// (BUG-311).
	void SkipRestOfStatement(Lexer& lexer);
}
