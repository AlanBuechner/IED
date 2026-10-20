// Provide the single logging implementation for this executable (see Core/Logging/Log.h).
#define IMPLEMENT_LOGGER
#include "Json/StdioChannel.h"
#include "Server/DapServer.h"

int main()
{
	Json::StdioChannel channel;
	C4DAP::Server::DapServer server([&channel](const std::string& framed) { channel.Write(framed); });
	Json::Value message;
	while (!server.ShouldExit() && channel.Read(message))
		server.HandleMessage(message);
	return 0;
}
