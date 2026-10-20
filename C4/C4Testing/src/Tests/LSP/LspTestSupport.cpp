#include "LspTestSupport.h"
#include <algorithm>

namespace Testing::Lsp
{
	C4LSP::Server::CompilationSession MakeSession(const std::string& name, const std::string& text)
	{
		C4LSP::Server::CompilationSession session;
		session.RegisterInMemorySource(name, text);
		session.Recompile();
		return session;
	}

	std::string UriFor(const std::string& name)
	{
		return C4LSP::Protocol::DocumentUri::ToUri(name);
	}

	C4LSP::Protocol::Position PositionOf(const std::string& text, const std::string& needle, int occurrence)
	{
		size_t searchFrom = 0;
		size_t found = std::string::npos;
		for (int i = 1; i <= occurrence; ++i)
		{
			found = text.find(needle, searchFrom);
			if (found == std::string::npos)
				return {}; // not enough occurrences
			searchFrom = found + needle.size();
		}

		uint32 line = 0;
		size_t lineStart = 0;
		for (size_t i = 0; i < found; ++i)
		{
			if (text[i] == '\n')
			{
				++line;
				lineStart = i + 1;
			}
		}

		C4LSP::Protocol::Position position;
		position.line = line;
		position.character = static_cast<uint32>(found - lineStart);
		return position;
	}

	bool Contains(const std::string& haystack, const std::string& needle)
	{
		return haystack.find(needle) != std::string::npos;
	}
} // namespace Testing::Lsp
