// The ied.gpu plugin's server code (docs/22 "Server and protocol"): each workspace's capture library, the programs
// launched for capture with their control pipes, and the replay sessions - one flashbang-replay process per open
// capture, shared by every client - behind the gpu.* methods and the gpu:<wsId> and gpurs:<sessionId> topics.
//
// The plugin's state changes inside PluginContext::Atomically, with the events that tell of it, so a topic's pulled
// snapshot reads it whole. A session's requests wait on the session's own lock, never the server's.

include "api/Plugin.ch"
include "api/Blobs.ch"
include "api/Launch.ch"

namespace GpuDebug
{
	// Where captures live, relative to a workspace's root.
	const char* c_CaptureFolder = ".ied/local/gpu-captures";
	// Where NativeFolder looks, and what puts the files there.
	const char* c_NoNativeParts = "the GPU debugger's native parts (flashbang.dll, flashbang-replay.exe) are not in the plugin's bin, nor in a checkout's build: an installed IED gets them from nuke install-ied in its checkout, a checkout from nuke build flashbang; or set gpu.nativeFolder to their folder";
	// A replay request waits this long for its answer (a timing run replays the frame many times).
	const int64 c_ReplayWaitMs = 600000;
	// A session nobody asked anything of for this long ends.
	const int64 c_SessionIdleMs = 1800000;

	int64 NowMs();
	Core::String LibraryTopic(Core::StringView wsId);

	// A capture in a workspace's library: its file and the sidecar the capture library wrote beside it.
	class Capture
	{
		public Core::String m_Id;          // the file's stem, unique in its folder
		public Core::String m_Path;        // the .flash file
		public Core::String m_Sidecar;     // the sidecar's JSON object ("{}" when it has none)
		public int64 m_Bytes;
		public bool m_Thumbnail;           // a .png beside it

		// {id, file, bytes, thumbnail?, meta}
		public Core::String Json(Core::StringView wsId) const;
	}

	class Library
	{
		public Core::String m_WsId;
		public Core::String m_Folder;      // kCaptureFolder of the workspace, in its local state (IED::WorkspaceFile)
		public Core::Array<Capture*> m_Captures;   // newest first

		public FS::Path Folder() const { return FS::Path(m_Folder.View()); }
		public Capture* Find(Core::StringView id) const;
		public Core::String Json() const;
	}

	// The captures of `root`'s library as they are on disk.
	Core::Array<Capture*> ScanCaptures(const FS::Path& folder);

	class Target
	{
		public Core::String m_Id;
		public Core::String m_WsId;
		public Core::String m_Name;        // the program's file name
		public Core::String m_Program;
		public int64 m_Pid;
		public Core::String m_State;       // running, exited
		public int64 m_ExitCode;
		public bool m_Connected;           // its capture library answers on the control pipe
		public Core::String m_Apis;        // a JSON array, from the library's hello
		public int64 m_Frame;
		public bool m_Capturing;
		public Core::String m_LastCapture; // the path of the last capture it wrote
		public Process::Child* m_Child;
		public Net::LocalStream* m_Pipe;
		public Threading::Mutex m_SendLock;

		public Core::String Json() const;
	}

	class Pending
	{
		public bool m_Done;
		public Core::String m_Result;      // the result's JSON
		public Core::String m_Error;
		public Core::Array<uint8> m_Binary;
	}

	// A range's experiments answer, by the EID they were asked for.
	class Experimented
	{
		public int64 m_Eid;
		public Core::String m_Answer;
	}

	class Session
	{
		public Core::String m_Id;
		public Core::String m_WsId;
		public Core::String m_CaptureId;
		public Core::String m_Path;
		public Core::String m_State;       // starting, ready, failed, ended
		public Core::String m_Problem;
		public Core::String m_Summary;     // initialize's result
		public Core::String m_CompareId;   // the capture it is compared with (docs/22 "Compare"); "" when none
		public Core::String m_Edits;       // the replayer's edits list (docs/22 "Edit and Replay"); "" when none
		// What the replay measured (docs/22 "Performance"), kept for clients that come back: the last timing and
		// compareTiming answers ("" when none) and each range's experiments.
		public Core::String m_Timing;
		public bool m_TimingStale;         // shaders were edited after it
		public Core::String m_CompareTiming;
		public Core::Array<Experimented*> m_Experiments;
		public Process::Child* m_Child;
		public Threading::Mutex m_Lock;
		public Threading::ConditionVariable m_Answered;
		public Core::HashMap<int64, Pending*> m_Pending;
		public int64 m_NextId;
		public int64 m_LastUsedMs;
		public bool m_Alive;

		public Core::String Topic() const { return Core::String("gpurs:") + m_Id.View(); }
		// {sessionId, wsId, captureId, state, problem?, summary?, compareWith?, edits?}
		public Core::String Json() const;
		// Edits change the frame: the timing is marked stale and what was measured with it dropped. Inside a change.
		public void Edited();
	}

	// An edit to compile (gpu.editShader, gpu_replay_with_shader): a workspace file's text as edited (`path`), else `text`.
	class ShaderEditRequest
	{
		public int64 m_Eid;
		public Core::String m_Stage;
		public Core::String m_Language;
		public Core::String m_Path;
		public Core::String m_Text;
		public Core::String m_Entry;
		public Core::String m_Profile;
	}

	class Debugger
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context) { this.m_Context = context; }

		public IED::PluginContext* m_Context;
		public Core::HashMap<Core::String, Library*> m_Libraries;
		public Core::Array<Target*> m_Targets;
		public Core::Array<Session*> m_Sessions;
		public int64 m_Made;

		// --- The library (Library.cf) ---
		public void Opened(const IED::Workspace& ws);
		public void Rescan(Core::StringView wsId);
		// {captures, targets, native}: the gpu:<wsId> topic's snapshot. Inside a change.
		public Core::String SnapshotJson(Core::StringView wsId);
		public Core::String TargetsJson(Core::StringView wsId);
		// The folder of flashbang.dll and flashbang-replay.exe; "" when they are not built.
		public Core::String NativeFolder(Core::StringView wsId);
		public Core::String NextId(const char* prefix);

		// --- Programs launched for capture (Targets.cf) ---
		public Target* Launch(const IED::Workspace& ws, Core::StringView program, const Core::Array<Core::String>& args, Core::StringView cwd, const Core::Array<Core::String>& environment);
		public void Changed(Target* target);
		public bool Send(Target* target, Core::StringView line);

		// --- Replay sessions (Sessions.cf) ---
		public Session* Open(Core::StringView wsId, Capture* capture);
		// One ied-gpu/1 request: the result's JSON, with `blob` the id of its binary part ("" when none).
		public Core::String Call(Session* session, Core::StringView method, Core::StringView params, Core::String& blob);
		// The same with the binary part as bytes.
		public Core::String Ask(Session* session, Core::StringView method, Core::StringView params, Core::Array<uint8>& binary);
		public void Close(Session* session, Core::StringView why);
		public void CloseIdle();
		// Opens `other` beside the session's capture in its replayer (compareOpen): compareOpen's result. Null stops
		// comparing and lets the other capture go.
		public Core::String Compare(Session* session, Capture* other);

		// --- What the replay measured (Measurements.cf) ---
		// Keeps a timing, compareTiming or experiments answer of the session's own capture.
		public void Keep(Session* session, Core::StringView method, Serialization::DomNode params, Core::StringView answer);
		// {timing?, timingStale, compareTiming?, experiments}: what the session keeps.
		public Core::String Measurements(Session* session);

		// --- Edit and Replay (Edits.cf) ---
		// shaderSource's answer for the session's capture, with the file the shader was last edited from (`link`).
		public Core::String ShaderSource(Session* session, const IED::Workspace& ws, Capture* capture, int64 eid, Core::StringView stage);
		// Compiles an edit into the session's replay: editShader's answer; the session's edits are published, and an edit
		// from a file that compiled is remembered for the shader.
		public Core::String EditShader(Session* session, const IED::Workspace& ws, Capture* capture, const ShaderEditRequest& edit);
		// One shader put back by its key, or all with an empty key: revertShader's answer.
		public Core::String RevertShader(Session* session, Core::StringView key);
		public void RefreshEdits(Session* session);

	private:
		private void Watch(Target* target);
		private void ReadOutput(Core::String wsId, Process::Child* child, Core::StringView label);
		private void Listen(Target* target);
		private void Heard(Target* target, Core::StringView line);
		private void ReadReplies(Session* session);
		private void Reply(Session* session, Core::StringView json, Core::Array<uint8>& binary);
		private void Ended(Session* session, Core::StringView problem);
	}

	void RegisterMethods(Debugger* debugger);
	// The project's debug configuration launched for capture (named, else the default), its pre-launch task run first;
	// refused in an untrusted workspace.
	Target* LaunchConfigured(Debugger* debugger, const IED::Workspace& ws, Core::StringView project, Core::StringView configurationName);
	// Asks the program's capture library for the next `frames` frames.
	void RequestCapture(Debugger* debugger, Target* target, int64 frames);
	// The gpu.query binding the gpu_* AI tools answer through (Tools.cf).
	void ProvideQuery(Debugger* debugger);
	void Activate(IED::PluginContext& context);
}
