// The ied.profiler plugin's server side (docs/16, docs/features/12-profiler.md): a TCP listener instrumented programs
// connect to, each connection a live capture streamed to a trace file as it arrives; each workspace's capture library
// in .ied/local/captures; captures opened on demand and kept in memory while they are looked at; the prof.* methods
// and the AI tools answered from them.
//
// Topics (pushed): profs:<wsId> - {captures, connect} the workspace's library and how a program connects; events
// "captures.changed" (the new list). prof:<captureId> - {status} of one capture; events "stats" while live, "ended",
// "annotations" ({annotations}, the whole list, when one is added, changed or removed).
//
// Threads: the accept loop, one thread per connection and the ticker that publishes live progress are threads of the
// plugin's own (StartThread); methods run on their requests' threads. The plugin's lock guards the workspaces and the
// capture list; each capture's own lock its contents. The plugin's lock is taken before a capture's, never after.

include "tracefile.ch"
include "annotations.ch"
include "api/plugin.ch"

namespace Profiler
{
	const char* kCapturesFolder = ".ied/local/captures";
	const char* kTraceExtension = ".iedtrace";
	const int64 kTickerNs = 250000000;
	const int32 kMostLoadedCaptures = 6;

	// A workspace the profiler serves: where its captures go and the token its programs connect with.
	class ProfWorkspace
	{
		public Core::String wsId;
		public Core::String root;
		public Core::String token;
		public FS::Path Captures() const { return FS::Path(root.View()) / kCapturesFolder; }
	}

	// One capture: a file in the library (or anywhere in the workspace), loaded when asked, or a live connection.
	class CaptureEntry
	{
		public Core::String id;          // "<wsId>:<16 hex digits of its path's hash>"
		public Core::String wsId;
		public Core::String path;        // absolute
		public Core::String relative;    // to the workspace's root
		public Capture* capture;         // null until loaded
		public bool live;
		public Net::TcpStream* connection;
		public int64 lastUsed;
		public int64 publishedEvents;
		public Core::String metadata;    // the index block's JSON, once known
		public int64 sizeBytes;
		public int64 modifiedNs;
		public Core::Array<Annotation> annotations;
		public bool annotationsRead;     // `annotations` holds the metadata's, read the first time they were asked for

		public Core::String Topic() const { return Core::String("prof:") + id.View(); }
	}

	class ProfilerPlugin
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		public void Opened(const IED::Workspace& ws);
		// Starts the listener, the accept loop and the ticker.
		public void Start();
		public void Stop();

		// The lock is held for these.
		public ProfWorkspace* Workspace(Core::StringView wsId) const;
		public ProfWorkspace* WorkspaceOfToken(Core::StringView token) const;
		public CaptureEntry* Entry(Core::StringView captureId) const;
		// The entry for a trace file of a workspace, made when new.
		public CaptureEntry* EntryForPath(ProfWorkspace* ws, Core::StringView absolutePath);
		// Reads the library folder again: new files get entries, removed ones lose theirs (unless live).
		public void Rescan(ProfWorkspace* ws);
		public Core::String LibraryJson(ProfWorkspace* ws);
		public Core::String ConnectEnvironment(const ProfWorkspace* ws) const;
		// Pushes profs:<wsId> again and says it changed.
		public void PublishLibrary(ProfWorkspace* ws);

		// The capture, loaded from its file when it is not in memory (without the lock while it reads); throws
		// not_found or unavailable.
		public Capture* Loaded(Core::StringView captureId);
		public void ForgetCapture(Core::StringView captureId);

		// A connection's life, on its own thread.
		public void Serve(Net::TcpStream* stream);

		// A capture's annotations, read from its file's metadata the first time. The lock is held.
		public Core::Array<Annotation>& AnnotationsOf(CaptureEntry* entry);
		// Keeps the file's metadata in step with the entry's annotations (a live capture's are written when it ends)
		// and pushes them to the capture's topic; throws unavailable when the file cannot take them. The lock is held.
		public void SaveAnnotations(CaptureEntry* entry);

		public IED::PluginContext* context;
		public Threading::Mutex lock;
		public Core::HashMap<Core::String, ProfWorkspace*> workspaces;
		public Core::HashMap<Core::String, CaptureEntry*> captures;
		public Net::TcpListener* listener;
		public int32 port;
		public bool stopping;
		public int64 clock;              // bumps on each use, for the least-recently-used unloading
	}

	// The capture id of a path in a workspace.
	Core::String CaptureIdFor(Core::StringView wsId, Core::StringView relativePath);
	// A file name for a new capture of `app` started at `unixNs`: "<app>-<yyyyMMdd-HHmmss>.iedtrace", made safe.
	Core::String CaptureFileName(Core::StringView app, int64 unixNs);
	// The metadata a capture's trace file ends with, ended for `ended` ("bye", "closed", "stopped").
	Core::String MetadataJson(const Capture& capture, Core::StringView ended);
	void RegisterMethods(ProfilerPlugin* profiler);
	// Answers the protocol method `name` with `method` (logged when it cannot).
	void RegisterMethod(ProfilerPlugin* profiler, const char* name, func<Core::String(ProfilerPlugin*, const IED::Params&)> method);
	// prof.annotations, prof.annotate, prof.updateAnnotation, prof.deleteAnnotation.
	void RegisterAnnotationMethods(ProfilerPlugin* profiler);
	// prof.allocations, prof.memoryTrend, prof.plots, prof.census, prof.hitches, prof.threads and prof.window (docs/16
	// "Finding problems").
	void RegisterInvestigationMethods(ProfilerPlugin* profiler);
	// Adds an annotation by `by` ("owner" or "ai") to a capture and saves it; throws not_found, bad_request or
	// unavailable. Takes the plugin's lock.
	Annotation AddAnnotation(ProfilerPlugin* profiler, Core::StringView captureId, int64 fromNs, int64 toNs, Core::StringView text, Core::StringView by);
	// The profiler.query server binding the profile_* AI tools answer through.
	void ProvideQuery(ProfilerPlugin* profiler);
}
