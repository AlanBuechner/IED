#pragma once
#include <memory>
#include <optional>
#include <string>

// Where a build server (C4.exe --serve-on, docs/compiler/07) takes its builds: a named pipe on Windows, a Unix socket
// elsewhere, one client at a time. A client sends a request line; while the server builds, the build's output - the
// compiler's and the tools' it runs - goes to the client (OutputToClient), then the answer line.
class ServeChannel
{
public:
	/// Listens as `name`; null when another server listens as `name` already, or it cannot (`error` says why).
	static std::unique_ptr<ServeChannel> Listen(const std::string& name, std::string& error);
	virtual ~ServeChannel() = default;

	/// Waits for the next client; false when none came for `idleSeconds`.
	virtual bool Accept(unsigned idleSeconds) = 0;
	/// The client's request line; nothing when it hung up.
	virtual std::optional<std::string> ReadLine() = 0;
	virtual void WriteLine(const std::string& line) = 0;
	/// Done with the client: what was written reaches it, then the connection ends.
	virtual void Hangup() = 0;

	/// While alive, the process's standard output and error (and the tools it starts) write to the client.
	class OutputToClient
	{
	public:
		explicit OutputToClient(ServeChannel& channel);
		~OutputToClient();
		OutputToClient(const OutputToClient&) = delete;
		OutputToClient& operator=(const OutputToClient&) = delete;

	private:
		ServeChannel& m_Channel;
	};

protected:
	virtual void RedirectOutput() = 0;
	virtual void RestoreOutput() = 0;
};
