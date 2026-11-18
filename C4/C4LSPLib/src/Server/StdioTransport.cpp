#include "Server/StdioTransport.h"
#include "Json/StdioChannel.h"
#include <memory>
#include <thread>

namespace C4LSP::Server
{
	int RunStdioLoop()
	{
		// A thread of its own takes each message off stdin as it arrives, so a request waiting for an analysis sees the
		// cancellations and edits behind it (MessageInbox). It may still be reading when the server exits: it owns what it
		// uses.
		auto channel = std::make_shared<Json::StdioChannel>();
		auto inbox = std::make_shared<MessageInbox>();
		std::thread([channel, inbox] {
			while (true)
			{
				Json::Value document;
				if (!channel->Read(document)) break;
				inbox->Push(std::move(document));
			}
			inbox->Close();
		}).detach();
		LspServer server([channel](const std::string& framed) { channel->Write(framed); });
		server.Serve(*inbox);
		return server.ExitCode();
	}
} // namespace C4LSP::Server
