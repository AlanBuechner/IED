#pragma once
// Shared helpers for the LSP unit tests. Plain utilities (no reflection) so they are not picked
// up as test classes by the reflection generator.
#include "Server/CompilationSession.h"
#include "Protocol/LspTypes.h"
#include <string>

namespace Testing::Lsp
{
	/// Build a compiled session from a single in-memory source file (plain file name, no folder).
	C4LSP::Server::CompilationSession MakeSession(const std::string& name, const std::string& text);

	/// The LSP URI that maps back to an in-memory source registered under `name`.
	std::string UriFor(const std::string& name);

	/// 0-based (line, character) of the n-th occurrence of `needle` in `text` (1 by default).
	/// Returns line 0 / char 0 when the needle is not found.
	C4LSP::Protocol::Position PositionOf(const std::string& text, const std::string& needle, int occurrence = 1);

	/// True when `haystack` contains `needle`.
	bool Contains(const std::string& haystack, const std::string& needle);
} // namespace Testing::Lsp
