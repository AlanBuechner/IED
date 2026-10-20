// Provide the single logging implementation for this executable (see Core/Logging/Log.h).
#define IMPLEMENT_LOGGER
#include "Server/StdioTransport.h"
#include <iostream>

int main()
{
	// All compiler logging goes to stderr: stdout carries the JSON-RPC protocol and must stay clean.
	std::cout.rdbuf(std::cerr.rdbuf());
	return C4LSP::Server::RunStdioLoop();
}
