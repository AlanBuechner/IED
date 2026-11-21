#pragma once
#include "Lexer/Token.h"
#include <vector>

namespace Scripting
{
	/// What brace recovery reads of one token (comments are left out): a brace or not, and the indentation of its line.
	struct BraceFact
	{
		TokenType type = TokenType::None;
		uint32 indent = 0;      // the column of the first token of its line
		bool startsLine = false; // the first token of its line
	};

	/// Where a file whose `{` and `}` do not pair up is mended: a `}` put before `before` for the `{` at `open` that is never
	/// closed, and the `}` a line's indentation shows closes nothing, left out.
	struct BraceRepairs
	{
		struct Closing
		{
			uint32 open = 0;   // the `{`
			uint32 before = 0; // the token the `}` goes before: the first line after the `{` indented no deeper than it
		};
		std::vector<Closing> closings;
		std::vector<uint32> dropped; // `}`s, by index

		bool Empty() const { return closings.empty() && dropped.empty(); }
	};

	/// The repairs of `facts` (one per token, the last the end of the file): none when its braces pair up. A `}` that starts
	/// a line closes the blocks opened on deeper-indented lines that are still open, while `{`s are missing, and is dropped
	/// when it is indented deeper than the `{` it would close, while `}`s are too many; a block still open at the end closes
	/// before the first line after it that is indented no deeper than its own.
	BraceRepairs PlanBraceRepairs(const std::vector<BraceFact>& facts);
}
