// Tor's control port (docs/29 "Tor"; Tor's control-spec): one connection, authenticated with the cookie file, commands
// answered by reply lines ("250-key=value", "250 OK"). It takes ownership, so Tor exits when the connection closes and a
// Tor whose server died does not outlive it. Not thread-safe: the service calls it under its lock.

include "bedrock.ch"

namespace Tor
{
	const int32 c_ControlMs = 5000;
	const int64 c_CookieBytes = 32;

	class TorControl
	{
		[operator("create")] public void Init() {}
		[operator("delete")] public void Delete() { Close(); }

		// Connects to 127.0.0.1:`port`, authenticates with the cookie in `cookieFile` and takes ownership of Tor; false
		// with `error`.
		public bool Open(int32 port, Core::StringView cookieFile, Core::String& error);
		public bool IsOpen() const { return m_Stream.IsOpen(); }
		// Sends `command` and reads its reply through the final line: the reply's lines, or false with `error` (a reply
		// of 400 and up, a closed connection, no reply in time).
		public bool Command(Core::StringView command, Core::Array<Core::String>& lines, Core::String& error);
		public void Close();

		private bool ReadLine(Core::String& line, Core::String& error);

		private Net::TcpStream m_Stream;
		private Core::String m_Received;   // bytes received and not yet read as lines
	}
}
