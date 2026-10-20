#include "Server/StdioTransport.h"
#include "Json/StdioChannel.h"

namespace C4LSP::Server
{
	int RunStdioLoop()
	{
		Json::StdioChannel channel;
		LspServer server([&channel](const std::string& framed) { channel.Write(framed); });
		Json::Value document;
		while (!server.ShouldExit() && channel.Read(document))
			server.HandleMessage(document);
		return server.ExitCode();
	}
} // namespace C4LSP::Server
