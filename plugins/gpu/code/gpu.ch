// The ied.gpu plugin's server code (docs/22 "Server and protocol"): each workspace's capture library, the programs
// launched for capture with their control pipes, and the replay sessions - one flashbang-replay process per open
// capture, shared by every client - behind the gpu.* methods and the gpu:<wsId> and gpurs:<sessionId> topics.
//
// The plugin's state changes inside PluginContext::Atomically, with the events that tell of it, so a topic's pulled
// snapshot reads it whole. A session's requests wait on the session's own lock, never the server's.

include "api/plugin.ch"
include "api/blobs.ch"
include "api/launch.ch"

namespace GpuDebug
{
	// Where captures live, relative to a workspace's root.
	const char* kCaptureFolder = ".ied/local/gpu-captures";
	// A replay request waits this long for its answer (a timing run replays the frame many times).
	const int64 kReplayWaitMs = 600000;
	// A session nobody asked anything of for this long ends.
	const int64 kSessionIdleMs = 1800000;

	int64 NowMs();
	Core::String LibraryTopic(Core::StringView wsId);

	// A capture in a workspace's library: its file and the sidecar the capture library wrote beside it.
	class Capture
	{
		public Core::String id;          // the file's stem, unique in its folder
		public Core::String path;        // the .flash file
		public Core::String sidecar;     // the sidecar's JSON object ("{}" when it has none)
		public int64 bytes;
		public bool thumbnail;           // a .png beside it

		// {id, file, bytes, thumbnail?, meta}
		public Core::String Json(Core::StringView wsId) const;
	}

	class Library
	{
		public Core::String wsId;
		public Core::String root;        // the workspace's folder
		public Core::Array<Capture*> captures;   // newest first

		public FS::Path Folder() const { return FS::Path(root.View()) / kCaptureFolder; }
		public Capture* Find(Core::StringView id) const;
		public Core::String Json() const;
	}

	// The captures of `root`'s library as they are on disk.
	Core::Array<Capture*> ScanCaptures(const FS::Path& folder);

	class Target
	{
		public Core::String id;
		public Core::String wsId;
		public Core::String name;        // the program's file name
		public Core::String program;
		public int64 pid;
		public Core::String state;       // running, exited
		public int64 exitCode;
		public bool connected;           // its capture library answers on the control pipe
		public Core::String apis;        // a JSON array, from the library's hello
		public int64 frame;
		public bool capturing;
		public Core::String lastCapture; // the path of the last capture it wrote
		public Process::Child* child;
		public Net::LocalStream* pipe;
		public Threading::Mutex sendLock;

		public Core::String Json() const;
	}

	class Pending
	{
		public bool done;
		public Core::String result;      // the result's JSON
		public Core::String error;
		public Core::Array<uint8> binary;
	}

	class Session
	{
		public Core::String id;
		public Core::String wsId;
		public Core::String captureId;
		public Core::String path;
		public Core::String state;       // starting, ready, failed, ended
		public Core::String problem;
		public Core::String summary;     // initialize's result
		public Process::Child* child;
		public Threading::Mutex lock;
		public Threading::ConditionVariable answered;
		public Core::HashMap<int64, Pending*> pending;
		public int64 nextId;
		public int64 lastUsedMs;
		public bool alive;

		public Core::String Topic() const { return Core::String("gpurs:") + id.View(); }
		// {sessionId, wsId, captureId, state, problem?, summary?}
		public Core::String Json() const;
	}

	class Debugger
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context) { this.context = context; }

		public IED::PluginContext* context;
		public Core::HashMap<Core::String, Library*> libraries;
		public Core::Array<Target*> targets;
		public Core::Array<Session*> sessions;
		public int64 made;

		// --- The library (library.cf) ---
		public void Opened(const IED::Workspace& ws);
		public void Rescan(Core::StringView wsId);
		// {captures, targets, native}: the gpu:<wsId> topic's snapshot. Inside a change.
		public Core::String SnapshotJson(Core::StringView wsId);
		public Core::String TargetsJson(Core::StringView wsId);
		// The folder of flashbang.dll and flashbang-replay.exe; "" when they are not built.
		public Core::String NativeFolder(Core::StringView wsId);
		public Core::String NextId(const char* prefix);

		// --- Programs launched for capture (targets.cf) ---
		public Target* Launch(const IED::Workspace& ws, Core::StringView program, const Core::Array<Core::String>& args, Core::StringView cwd, const Core::Array<Core::String>& environment);
		public void Changed(Target* target);
		public bool Send(Target* target, Core::StringView line);

		// --- Replay sessions (sessions.cf) ---
		public Session* Open(Core::StringView wsId, Capture* capture);
		// One ied-gpu/1 request: the result's JSON, with `blob` the id of its binary part ("" when none).
		public Core::String Call(Session* session, Core::StringView method, Core::StringView params, Core::String& blob);
		// The same with the binary part as bytes.
		public Core::String Ask(Session* session, Core::StringView method, Core::StringView params, Core::Array<uint8>& binary);
		public void Close(Session* session, Core::StringView why);
		public void CloseIdle();

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
	// The gpu.query binding the gpu_* AI tools answer through (tools.cf).
	void ProvideQuery(Debugger* debugger);
	void Activate(IED::PluginContext& context);
}
