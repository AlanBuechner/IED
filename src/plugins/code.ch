// Plugin code (docs/26 section 2): a plugin whose runtime is "c4" has C4 code the server compiles with its script host
// (C4/docs/runtime/04) against its interface, allowed to include only api/ (and Bedrock); a built-in's ("builtin" with
// an entry) is compiled into the server and found by reflection. Each running copy - one for a built-in or app plugin,
// one per workspace for a project plugin - has its own thread: its compile, its entry, its bindings, tool calls,
// sections and event handlers all run there, one at a time and never holding the server lock; callers queue a job and
// wait for it with the server lock released. Its protocol methods run on their requests' threads, counted, so a script
// unloads only once they have returned. The code starts on its first activation event (or at load when it lists none)
// and a script is compiled again when its files change; a new build that does not compile leaves the running one in
// place with the diagnostics as its problem.

include "manifest.ch"
include "api/plugin.ch"
include "scripting.ch"
include "fs/watching.ch"

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
		public Core::Array<IED::WorkspaceHandler> trustHandlers;
		public Core::Array<IED::FilesHandler> filesHandlers;
		public Core::Array<func<void()>> deactivateHandlers;
	}

	// A hub topic a plugin made: its snapshot pushed as text, or pulled from its function when a client subscribes.
	class PluginTopic
	{
		public Core::String key;
		public Core::String text;
		public func<Core::String()> pull;
		public bool pulled;
	}

	// A folder a plugin watches outside every workspace; `stopped` once the plugin stops (the watcher's thread runs on).
	class PluginWatch
	{
		public FolderWatch* watch;
		public bool stopped;
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
		public bool builtin;                 // compiled into the server: found by reflection, never reloaded

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
		public Core::Array<Core::String> methods;     // the protocol methods it registered
		public Core::Array<PluginTopic*> topics;
		public Core::Array<PluginWatch*> watches;
		public bool wantsTrust;              // it has OnTrustChanged handlers
		public bool wantsFiles;              // it has OnFilesChanged handlers
		public int64 running;                // its methods running now
		public Threading::ConditionVariable idle;
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
		// A workspace was trusted or untrusted: the OnTrustChanged handlers of the code loaded in it run.
		public void TrustChanged(Workspace* ws);
		// What a workspace's watcher saw (hidden folders included): the OnFilesChanged handlers run.
		public void FilesChanged(Workspace* ws, const ChangeBatch& batch);
		// A request for a method `instance`'s code registered, run on the request's thread with the lock released.
		public Core::String CallMethod(CodeInstance* instance, const IED::MethodHandler& handler, Call& call, const Params& params);
		// The snapshot of a pulled topic, from its function (the lock is held, as the hub asks for it).
		public Core::String PulledSnapshot(CodeInstance* instance, PluginTopic* topic);
		// The topic `key` of `instance`'s, or null.
		public PluginTopic* TopicOf(CodeInstance* instance, Core::StringView key) const;
		// Whether `instance` may name a method or topic `name`: a built-in any free one, another "<pluginId>/...".
		public bool MayName(CodeInstance* instance, Core::StringView name) const;
		// A watched folder's changes (from the watcher's thread, without the lock): `handler` runs on the plugin's thread.
		public void QueueWatched(CodeInstance* instance, PluginWatch* watch, func<void(const Core::Array<Core::String>&)> handler, const Core::Array<Core::String>& paths);
		// A protocol event (the hub's tap): queued for the code that listens to its kind.
		public void Published(Core::StringView topic, Core::StringView kind, Core::StringView data);
		// The running code of `plugin` as loaded for `ws`, or null when it has none.
		public CodeInstance* For(Plugin* plugin, Workspace* ws) const;
		// The code that provides the server binding `name` for `ws` (null: the built-in and app plugins).
		public CodeInstance* Provider(Workspace* ws, Core::StringView name) const;
		// Whether `instance` is loaded for `ws`: a project plugin's own workspace, or a global one that no project plugin
		// of `ws` overrides.
		public bool LoadedIn(CodeInstance* instance, Workspace* ws) const;
		// The scopes a C4 language server checks plugin code in (docs/26 "Editing plugin code"): one per script plugin
		// loaded for `ws`, then one per script plugin `ws` holds in another folder - its source folders, the server's
		// interface and the allow-list - as a JSON array.
		public Core::String ScopesJson(Workspace* ws) const;
		// The protocol methods and topics the running code registered, added to `methods` and `topics` (the master's
		// routes, docs/10 "Routing").
		public void Registered(Core::Array<Core::String>& methods, Core::Array<Core::String>& topics) const;
		// Whether some code is still starting (compiling, or its entry not yet run).
		public bool Starting() const;
		// The plugin `instance` runs the code of, as loaded now; null when it is gone.
		public Plugin* PluginOf(CodeInstance* instance) const;
		// Runs `body` on `instance`'s thread, starting its code first when it has not started, and waits at most `ms`
		// (more while it compiles) with the lock released, or until `stopped()`; false with `problem` when the code cannot
		// run or did not finish in time. On the instance's own thread `body` runs at once.
		public bool Run(CodeInstance* instance, func<void(CodeInstance*)> body, int64 ms, func<bool()> stopped, Core::String& problem);
		// The same on a thread of its own, as the instance's code, so a long call (a build) leaves the instance's thread
		// to its other calls; the code started first if it has not. The lock is held.
		public bool RunApart(CodeInstance* instance, func<void(CodeInstance*)> body, int64 ms, func<bool()> stopped, Core::String& problem);
		// A guarded call faulted: the code fails until it is built again. Takes the lock; FaultedLocked with it held.
		public void Faulted(CodeInstance* instance, Core::StringView problem);
		public void FaultedLocked(CodeInstance* instance, Core::StringView problem);
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
		// Its methods, topics and watches go (the lock is held).
		private void Unregister(CodeInstance* instance);
		// Runs a new build's entry (a script's, or a built-in's in the server); with `compileMs` for the log.
		private void Enter(CodeInstance* instance, Core::StringView digest, int64 compileMs);
		private void BuildScript(CodeInstance* instance);
		private void Failed(CodeInstance* instance, Core::StringView problem, Core::StringView digest);
		private bool Wants(CodeInstance* instance, Workspace* ws) const;
		private void Loop(CodeInstance* instance);
		// Waits for `job` with the lock released, at most `wait` ms or until `stopped()`.
		private bool Await(CodeInstance* instance, CodeJob* job, int64 wait, func<bool()> stopped, Core::String& problem);
		private Scripting::Host* ScriptHost(Core::String& problem);
		private void UseScriptCache();

		private Core::Array<CodeInstance*> instances;
		private Scripting::Host* host;
		private Core::String hostProblem;
		private bool hostTried;
	}

	// Whether `plugin`'s manifest declares the output source `source` (outputSources).
	bool DeclaresOutput(Plugin* plugin, Core::StringView source);
	// The .cf files of `instance`'s sources, in name order.
	Core::Array<Core::String> CodeFilesOf(CodeInstance* instance);
	// The running code on this thread, or null when the thread runs no plugin's code.
	CodeInstance* CurrentCode();
	void SetCurrentCode(CodeInstance* instance);
	// `ws` as the plugin API shows it; with the server lock held.
	IED::Workspace PluginWorkspace(Server* server, Workspace* ws);
	// Runs `body` inside `instance`'s script, guarded (C4/docs/runtime/04 section 7; a built-in's directly): false with
	// `problem` when it threw or faulted - a fault fails the plugin. Without the lock held.
	bool GuardedCall(CodeInstance* instance, func<void()> body, Core::String& problem);
	// The same with the lock held.
	bool GuardedCallLocked(CodeInstance* instance, func<void()> body, Core::String& problem);
	// A free function of the code by its qualified name (a built-in's in the server's own reflection), checked against
	// the signature its use expects; invalid with `problem` when there is none such.
	Reflection::Function CodeFunction(CodeInstance* instance, Core::StringView name, Core::StringView returns, Core::StringView parameter, Core::String& problem);
	// Calls `function` with one argument at `argument`, its result into `result`, guarded as GuardedCall.
	bool InvokeCode(CodeInstance* instance, const Reflection::Function& function, void* result, void* argument, Core::String& problem);

	// The plugin API's caller: the running code on this thread; null with `problem` outside a plugin's code.
	CodeInstance* ApiCaller(Core::String& problem);
	// The open workspace `wsId` when `instance` holds `permission` (empty: none needed) and is loaded there; null with
	// `problem` otherwise. With the lock held.
	Workspace* PermittedWorkspace(CodeInstance* instance, Core::StringView wsId, const char* permission, Core::String& problem);

	// The server lock as the plugin API takes it: not again on a thread that holds it already (inside IED::HostLock,
	// PluginContext::Atomically, or a pulled snapshot's function).
	class ApiLock
	{
		[operator("create")] public void Init(Server* server);
		[operator("delete")] public void Delete();

		private Server* server;
		private bool taken;
	}

	// Whether this thread holds the server lock for plugin code; set while it does.
	bool HostLockHeld();
	void SetHostLockHeld(bool held);

	// The server lock released for plugin code to run in for a scope (a job, a method, a call into the plugin's own
	// binding), with the thread's HostLockHeld cleared meanwhile, so the code's API calls take the lock again instead of
	// believing they hold it; both come back when the scope ends. The thread holds the lock when it begins.
	class CodeUnlocked
	{
		[operator("create")] public void Init(Server* server);
		[operator("delete")] public void Delete();

		private Server* server;
		private bool held;
	}

	// The thread's running code for a call made on another thread than the plugin's (a method, a pulled snapshot).
	class CodeScope
	{
		[operator("create")] public void Init(CodeInstance* instance);
		[operator("delete")] public void Delete();

		private CodeInstance* saved;
	}

	// A wait nothing stops but its time.
	func<bool()> NeverStopped();

	const int64 kCodeCallMs = 30000;          // a section or a binding call
	const int64 kCodeStartMs = 120000;        // a first call waits for the code to compile
	const char* kCodeAllowed = "api/";        // the script allow-list
}
