// Plugin code (docs/26 section 2): a plugin whose runtime is "c4" has C4 code the server compiles with its script host
// (C4/docs/runtime/04) against its interface, allowed to include only api/ (and Bedrock). Each running copy - one for
// a built-in or app plugin, one per workspace for a project plugin - has its own thread: its compile, its entry, its
// bindings, tool calls, sections and event handlers all run there, one at a time and never holding the server lock, so
// the script is never unloaded under a call. Callers queue a job and wait for it with the server lock released.
// The code starts on its first activation event (or at load when it lists none) and is compiled again when its files
// change; a new build that does not compile leaves the running one in place with the diagnostics as its problem.

include "manifest.ch"
include "api/plugin.ch"
include "scripting.ch"

namespace Ied
{
	enum class CodeState { Idle, Compiling, Active, Failed, Stopped }

	const char* CodeStateName(CodeState state);

	// Work for a plugin's thread. `done` is set under the server lock.
	class CodeJob
	{
		public func<void()> body;
		public bool done;
	}

	// What the code registered through its context; only its thread touches it.
	class CodeHandlers
	{
		public Core::HashMap<Core::String, IED::Binding> bindings;
		public Core::Array<Core::String> listenKinds;
		public Core::Array<IED::EventHandler> listeners;
		public Core::Array<IED::WorkspaceHandler> workspaceHandlers;
		public Core::Array<func<void()>> deactivateHandlers;
	}

	// A computed contribution: entries of `point` in workspace `wsId` ("" every workspace).
	class Contributed
	{
		public Core::String point;
		public Core::String wsId;
		public Serialization::DomDoc* doc;
		public Serialization::DomNode entries;
	}

	// One running copy of a plugin's code.
	class CodeInstance
	{
		public Core::String pluginId;
		public Core::String wsId;            // a project plugin's workspace; empty for a built-in or app plugin
		public FS::Path folder;
		public Core::String entry;
		public Core::Array<Core::String> sources;
		public Core::Array<Core::String> permissions;
		public Core::Array<Core::String> activation;
		public Core::String digest;          // of its sources, for the build that runs

		// Under the server lock.
		public CodeState state;
		public Core::String problem;         // its diagnostics, its fault, or why it cannot run
		public Core::Queue<CodeJob*> queue;
		public Threading::ConditionVariable wake;
		public Threading::ConditionVariable finished;
		public bool stopping;
		public Core::HashMap<Core::String, bool> provides;   // the bindings its code provides
		public Core::HashMap<Core::String, bool> hears;      // the event kinds it listens to
		public Core::Array<Contributed*> contributed;
		public int64 builds;                 // how many times it compiled
		public bool buildQueued;             // a build is queued: it reads the files when it starts
		public CodeRuntime* runtime;

		// On its thread only.
		public Scripting::Script* script;
		public IED::PluginContext* context;
		public CodeHandlers handlers;
		public Threading::Thread thread;

		public bool May(Core::StringView permission) const;
		public bool IsProject() const { return !wsId.IsEmpty(); }
	}

	class CodeRuntime
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The lock is held for all of these.
		// Brings the running code in line with the plugins loaded: `ws` null for the built-in and app plugins, else
		// `ws`'s project plugins; new ones start (on an activation event of theirs), changed ones compile again, and
		// those no longer loaded stop.
		public void Reconcile(Workspace* ws);
		// Builds again every code that failed (plugins.reload).
		public void RetryFailed();
		// A workspace opened: its project plugins reconcile and the activation events and OnWorkspace handlers run.
		public void WorkspaceOpened(Workspace* ws);
		// A protocol event (the hub's tap): queued for the code that listens to its kind.
		public void Published(Core::StringView topic, Core::StringView kind, Core::StringView data);
		// The running code of `plugin` as loaded for `ws`, or null when it has none.
		public CodeInstance* For(Plugin* plugin, Workspace* ws) const;
		// The code that provides the server binding `name` for `ws` (null: the built-in and app plugins).
		public CodeInstance* Provider(Workspace* ws, Core::StringView name) const;
		// Whether `instance` is loaded for `ws`: a project plugin's own workspace, or a global one that no project plugin
		// of `ws` overrides.
		public bool LoadedIn(CodeInstance* instance, Workspace* ws) const;
		// The plugin `instance` runs the code of, as loaded now; null when it is gone.
		public Plugin* PluginOf(CodeInstance* instance) const;
		// Runs `body` on `instance`'s thread, starting its code first when it has not started, and waits at most `ms`
		// (more while it compiles) with the lock released, or until `stopped()`; false with `problem` when the code cannot
		// run or did not finish in time. On the instance's own thread `body` runs at once.
		public bool Run(CodeInstance* instance, func<void(CodeInstance*)> body, int64 ms, func<bool()> stopped, Core::String& problem);
		// A guarded call faulted: the code fails until it is built again. Takes the lock.
		public void Faulted(CodeInstance* instance, Core::StringView problem);
		// The entries `instance`'s code computed for `point` in `ws`.
		public void ComputedFor(CodeInstance* instance, Workspace* ws, Core::StringView point, Core::Array<Serialization::DomNode>& into) const;
		// The JSON for plugins.list's "code" field.
		public Core::String SummaryJson(CodeInstance* instance) const;
		public const Core::Array<CodeInstance*>& All() const { return instances; }

		public Server* server;

	private:
		private CodeInstance* Start(Plugin* plugin, Workspace* ws);
		private void Stop(CodeInstance* instance);
		private void Queue(CodeInstance* instance, func<void()> body);
		private void Activate(CodeInstance* instance);
		private void Build(CodeInstance* instance);
		private void Retire(CodeInstance* instance);
		private void Failed(CodeInstance* instance, Core::StringView problem, Core::StringView digest);
		private bool Wants(CodeInstance* instance, Workspace* ws) const;
		private void Loop(CodeInstance* instance);
		private Scripting::Host* ScriptHost(Core::String& problem);

		private Core::Array<CodeInstance*> instances;
		private Scripting::Host* host;
		private Core::String hostProblem;
		private bool hostTried;
	}

	// The .cf files of `instance`'s sources, in name order.
	Core::Array<Core::String> CodeFilesOf(CodeInstance* instance);
	// The running code on this thread, or null when the thread runs no plugin's code.
	CodeInstance* CurrentCode();
	// `ws` as the plugin API shows it; with the server lock held.
	IED::Workspace PluginWorkspace(Server* server, Workspace* ws);
	// Runs `body` inside `instance`'s script, guarded (C4/docs/runtime/04 section 7): false with `problem` when it threw
	// or faulted - a fault fails the plugin. On the instance's thread.
	bool GuardedCall(CodeInstance* instance, func<void()> body, Core::String& problem);
	// A free function of the code by its qualified name, checked against the signature its use expects: its address, or
	// null with `problem`. On the instance's thread.
	void* CodeFunction(CodeInstance* instance, Core::StringView name, Core::StringView returns, Core::StringView parameter, Core::String& problem);

	// A wait nothing stops but its time.
	func<bool()> NeverStopped();

	const int64 kCodeCallMs = 30000;          // a section or a binding call
	const int64 kCodeStartMs = 120000;        // a first call waits for the code to compile
	const char* kCodeAllowed = "api/";        // the script allow-list
}
