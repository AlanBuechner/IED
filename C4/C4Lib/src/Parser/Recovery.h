#pragma once
#include "Lexer/Lexer.h"
#include "Parser/Nodes/ErrorNode.h"
#include "Core/Core.h"

namespace C4Compiler
{
	/// What the parse of a body goes on with after a piece of it did not parse.
	enum class Resume
	{
		Declaration, // the next declaration of a file or namespace
		Member,      // the next member of a type
		Statement,   // the next statement of a function body
	};

	/// Skips the broken piece at the lexer's position: past its `;`, or past a `{ ... }` group outside parentheses that
	/// ends its line (and a `;` right after one, but for a statement; a lambda's body inside a call does not end it), or
	/// up to the `}` that closes the enclosing body, or up to a line that starts the next piece of `resume` (`struct`,
	/// `public`, `return`, ...). A stray `}` at a file's root is skipped alone. Returns the error node over the skipped
	/// tokens, saying `why`.
	Ref<ErrorNode> SkipBroken(Lexer& lexer, Resume resume, Core::String why);
}
