// Provide the single logging implementation for this executable (see Core/Logging/Log.h).
#define IMPLEMENT_LOGGER
#include "Server/StdioTransport.h"
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
	// The server analyzes one program after another for as long as it runs, each hundreds of megabytes it lets go:
	// the heap hands freed memory back to the system rather than keeping it for a peak it may never see again.
	void ReturnFreedHeapMemory()
	{
#ifdef _WIN32
		HEAP_OPTIMIZE_RESOURCES_INFORMATION information = { HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION, 0 };
		HeapSetInformation(nullptr, HeapOptimizeResources, &information, sizeof(information));
#endif
	}
}

int main()
{
	ReturnFreedHeapMemory();
	// All compiler logging goes to stderr: stdout carries the JSON-RPC protocol and must stay clean.
	std::cout.rdbuf(std::cerr.rdbuf());
	return C4LSP::Server::RunStdioLoop();
}
