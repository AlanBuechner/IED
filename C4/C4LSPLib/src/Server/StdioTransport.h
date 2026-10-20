#pragma once
#include "Server/LspServer.h"
#include <string>

namespace C4LSP::Server
{
	/// Reads LSP messages from stdin, decodes them, feeds the server, and writes the server's
	/// framed output to stdout. Returns the process exit code (0 for a clean shutdown/exit).
	int RunStdioLoop();
} // namespace C4LSP::Server
