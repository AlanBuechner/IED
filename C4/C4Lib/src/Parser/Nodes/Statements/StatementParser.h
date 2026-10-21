#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"

namespace Scripting
{
	/// One statement of any kind - control flow, a block, a label, a destructuring, a declaration or an expression - as a
	/// block parses each of its statements and every single-statement body (`if`, `else`, loops, `case`, `catch`) parses
	/// its one. nullptr, with nothing consumed, when no statement starts here.
	Ref<AstNode> ParseAnyStatement(ParserState& state);
}
