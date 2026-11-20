#pragma once
#include "Core/Core.h"
#include "Lexer/Token.h"

namespace Scripting
{
	/// An include a file's parse asked the compiler for, in the order it asked: what it names, where, and the error the
	/// answer was (empty when the file was found). The answer depends on the other files, so a parse taken again for the
	/// same text asks again (Compiler/PriorParses).
	struct IncludeAsked
	{
		Core::String path;
		SourceLocation at;
		Core::String error;
		bool afterErrorHere = false; // the parse reported an error at `at` just before: an error of the answer is dropped
	};
} // namespace Scripting
