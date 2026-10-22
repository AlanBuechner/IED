#include "StringFormat.h"
#include <charconv>
#include <cstring>


namespace Core
{

	// A '{' starts a placeholder only when it is closed by '}' and the content between them is
	// entirely digits. Anything else (unterminated brace, empty braces, non-numeric content) is
	// literal text - a formatting typo must never discard the whole message, because callers such
	// as ErrorTracker drop empty diagnostics entirely. Doubling the braces renders one: "{{0}}".

	bool StringFormat::ParseMessage(const char* msg, Vector<MessageToken>& tokens)
	{
		uint32 findex = 0;
		while (msg[findex] != 0)
		{
			uint32 sindex = findex;
			// Literal text runs up to '{' or an escaped brace '\{'.
			while (msg[findex] != 0 && msg[findex] != '{' && !(msg[findex] == '\\' && msg[findex + 1] == '{'))
				findex++;

			tokens.Push(MessageToken(std::string_view(&msg[sindex], findex - sindex)));

			if (msg[findex] == 0)
				break;

			if (msg[findex] == '\\')
			{
				// '\{' renders a literal brace that can never be parsed as a placeholder.
				tokens.Push(MessageToken(std::string_view("{", 1)));
				findex += 2;
				continue;
			}

			// At '{': look for the matching '}' and check whether the content is a valid index.
			findex++; // skip the {
			uint32 close = findex;
			while (msg[close] != 0 && msg[close] != '}')
				close++;

			if (msg[close] == 0 || close == findex)
			{
				// Unterminated or empty braces: the rest of the message is literal text.
				tokens.Push(MessageToken(std::string_view(&msg[findex - 1], std::strlen(&msg[findex - 1]))));
				break;
			}

			std::string_view indexString(&msg[findex], close - findex);
			uint32 index = 0;
			auto result = std::from_chars(indexString.data(), indexString.data() + indexString.size(), index);
			if (result.ec == std::errc::invalid_argument || result.ptr != indexString.data() + indexString.size())
			{
				// Not a valid placeholder: keep the braces as literal text.
				tokens.Push(MessageToken(std::string_view(&msg[findex - 1], close - findex + 2)));
				findex = close + 1;
				continue;
			}

			tokens.Push(MessageToken(index));
			findex = close + 1;
		}
		return true;
	}

}

