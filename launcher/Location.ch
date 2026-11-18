// Locations as people type them, "C:\src\a.cf:42:5", and the control channel's `open` request for one.

include "bedrock.ch"

namespace Launch
{
	// "C:\src\a.cf:42:3" to the path, line and column (0: none); a Windows drive's colon stays in the path.
	void SplitLocation(Core::StringView text, Core::String& path, int64& line, int64& col);

	// What `open` asks for (docs/10).
	class OpenRequest
	{
		public Core::String m_Path;   // a folder or file
		public int64 m_Line;          // 0: none
		public int64 m_Col;
		public Core::String m_In;     // the workspace to open it in, a solution file or a folder holding one; empty: the one it belongs to
		public bool m_Reveal;         // a window that has its workspace open already is asked to show it
	}

	// A folder or file opened in the workspace it belongs to, at its start.
	OpenRequest OpenOf(Core::StringView path);
	// The params of `open`: the request with its paths made absolute.
	Core::String OpenParams(const OpenRequest& request);
}
