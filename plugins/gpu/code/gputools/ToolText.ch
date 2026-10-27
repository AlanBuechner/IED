// The AI's view of the GPU debugger (docs/13 T-GPU, docs/22 "AI"): each gpu_* tool's answer as compact text a model
// reads well, from the capture's replay session - the same answers the UI shows. A tool names a capture by its id
// (gpu_captures) or "latest", the default; events by EID; resources by id (gpu_resources, or the [#id] the state and
// event answers print).
include "Gpu.ch"

namespace GpuDebug
{
	// A tool's answer: its text and, for gpu_texture, a PNG relative to the workspace root ("" when none).
	class ToolAnswer
	{
		public Core::String m_Text;
		public Core::String m_Image;
	}

	// The answer of tool `tool` ("captures", "events", "state", ...) with `args` in `ws`; throws an RpcError for a bad
	// request.
	ToolAnswer AnswerTool(Debugger* debugger, const IED::Workspace& ws, Core::StringView tool, const Serialization::DomNode& args);

	// --- What the answers share (ToolText.cf) ---

	// The capture a tool names and its replay session, opened when it is not.
	class ToolCapture
	{
		public Capture* m_Capture;
		public Session* m_Session;
		public Core::String m_WsId;
		public IED::Workspace m_Workspace;
		public Core::String m_Api;
	}

	const int64 c_TimingRepeat = 5;

	// The capture `asked` names in `ws`: an id, or "latest"; throws NotFound.
	Capture* CaptureNamed(Debugger* debugger, const IED::Workspace& ws, Core::StringView asked);
	ToolCapture OpenNamed(Debugger* debugger, const IED::Workspace& ws, const Serialization::DomNode& args);
	Serialization::DomNode ParseAnswer(Core::StringView json);
	Serialization::DomNode AskTool(Debugger* debugger, const ToolCapture& target, const char* method, Core::StringView params);
	// The resource an argument names, else what event `eid` writes first: its render target, depth target or storage
	// resource (0 when it writes none).
	int64 ResourceArg(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args, int64 eid);
	Core::String ResourceName(Debugger* debugger, const ToolCapture& target, int64 id);
	int64 IntArg(const Serialization::DomNode& args, const char* key, int64 fallback);
	// Milliseconds to 2 to 4 decimals, as small times need: "0.0052 ms".
	void AppendMs(Core::StringBuilder& text, double ms);
	// A value as the UI shows one: integers whole, others to 6 significant digits, NaN and Inf by name.
	void AppendValue(Core::StringBuilder& text, double value);
	// Whether `text` holds `part`, ASCII letters in either case.
	bool HasFolded(Core::StringView text, Core::StringView part);

	// --- The answers ---

	Core::String CapturesText(Debugger* debugger, const IED::Workspace& ws);
	Core::String CaptureText(Debugger* debugger, const IED::Workspace& ws, const Serialization::DomNode& args);
	Core::String EventsText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String ResourcesText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String TimingText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String StateText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String ShaderText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String PixelText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String BufferText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	ToolAnswer TextureAnswer(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String ScanText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String DebugText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String CompareText(Debugger* debugger, const IED::Workspace& ws, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String ReplayWithShaderText(Debugger* debugger, const IED::Workspace& ws, const ToolCapture& target, const Serialization::DomNode& args);
	Core::String LimiterText(Debugger* debugger, const ToolCapture& target, const Serialization::DomNode& args);
	// A texel difference (compareTexture's or editDiff's answer) as a line: the output, how many pixels differ and where.
	void AppendImageDifference(Core::StringBuilder& text, const Serialization::DomNode& image);
}
