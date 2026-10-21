// The plugin API (docs/26 section 2): the part of the server a plugin's C4 code includes - this folder is the script
// allow-list, and Bedrock is always allowed. The server compiles a plugin's code (runtime.sources) against its
// interface when the plugin activates and again when the code changes. Its entry (runtime.entry) is a free function
// `void <Name>(IED::PluginContext& context)` that registers what the plugin provides; its manifest's `function`
// entries name free functions of the code:
//     toolCalls               IED::ToolResult <Name>(const IED::ToolCall& call)        (tools.ch)
//     systemMessageSections   Core::String <Name>(const IED::Workspace& ws)            the section's text, "" for none
// A plugin's code runs on the plugin's own thread, one call at a time, never holding the server's lock - but its protocol
// methods, on their requests' threads (docs/26 "Built-ins on the API"); a hardware fault in a script's code stops that
// call and marks the plugin failed until its code changes or it is reloaded. A built-in plugin's code is compiled into
// the server (runtime.kind "builtin" with an entry) and uses this same API.

include "workspace.ch"
include "bindings.ch"
include "tools.ch"
include "process.ch"
include "methods.ch"
include "text.ch"
include "documents.ch"
include "tasks.ch"
include "contributions.ch"

namespace IED
{
	// The events a change publishes with it (PluginContext::Atomically); valid only inside the change.
	class Events
	{
		[operator("create")] public void Init(void* plugin) { host = plugin; }

		public void Publish(Core::StringView key, const char* kind, Core::StringView data);
		public void PublishMergeable(Core::StringView key, const char* kind, const Merge& merge);
		// Waits on `condition` (notified inside another change) with the server's lock released, at most `ns`
		// nanoseconds (negative: until notified); the change goes on holding it again.
		public void Wait(Threading::ConditionVariable& condition, int64 ns);

		private void* host;
	}

	class PluginContext
	{
		[operator("create")] public void Init(void* plugin) { host = plugin; }

		public Core::String Id() const;
		// The plugin's folder.
		public Core::String Folder() const;
		// The workspace a project plugin was loaded for; "" for a built-in or app plugin.
		public Core::String WorkspaceId() const;
		// A folder for the plugin's machine-local state that belongs to no workspace - caches, the profile of a program
		// it runs: <the server's data folder>/plugins/<id>, made when asked for. Any thread.
		public Core::String StateFolder() const;

		// A line in the server's log (the Output window's IED source), as "[<id>] <line>". Any thread.
		public void Log(Core::StringView line) const;
		// A setting's effective value as JSON text ("null" when it has none), in `wsId` ("": the app's). Any thread.
		public Core::String Setting(Core::StringView key, Core::StringView wsId) const;

		// Answers calls of `name`, a server binding the manifest lists under bindings.provides.
		public void Provide(Core::StringView name, Binding handler);
		// Hears the protocol events of `kind` (docs/10: "tree.changed", "task.status", ...) on every topic.
		public void Listen(Core::StringView kind, EventHandler handler);
		// Runs `opened` for each workspace the plugin is loaded for: those open now, then each that opens.
		public void OnWorkspace(WorkspaceHandler opened);
		// Runs when the plugin stops: disabled, removed, or reloaded with changed code.
		public void OnDeactivate(func<void()> handler);
		// Runs when a workspace the plugin is loaded for is trusted or untrusted.
		public void OnTrustChanged(WorkspaceHandler handler);
		// A workspace's file changes as its watcher batches them, hidden folders (.git) included.
		public void OnFilesChanged(FilesHandler handler);
		// Watches a folder outside every workspace; `handler` gets the paths changed in it (relative to it), on the
		// plugin's thread. False with `problem` when it cannot be watched. Any thread.
		public bool WatchFolder(Core::StringView folder, func<void(const Core::Array<Core::String>&)> handler, Core::String& problem);

		// Answers the protocol method `name` (docs/10): "<pluginId>/<method>", or a built-in's protocol name. False with
		// `problem` when the name is not the plugin's to take.
		public bool Method(Core::StringView name, MethodHandler handler, Core::String& problem);
		// The topic `key` (a built-in's protocol name, or "<pluginId>/..."), with `snapshot` (JSON) as what a client gets
		// when it subscribes, until the next call. Any thread.
		public void Topic(Core::StringView key, Core::StringView snapshot);
		// The topic `key` whose snapshot `snapshot()` gives when a client subscribes - with the server's lock held, so it
		// takes no lock the plugin holds while it calls the API. Any thread.
		public void TopicFrom(Core::StringView key, func<Core::String()> snapshot);
		// An event of `kind` with `data` (a JSON object) on the plugin's topic `key`. Any thread.
		public void Publish(Core::StringView key, const char* kind, Core::StringView data);
		// An event a slow client may get merged with the next of its kind (IED::Merge). Any thread.
		public void PublishMergeable(Core::StringView key, const char* kind, const Merge& merge);
		// Runs `change` with the server's lock held: what it changes of the plugin's state and the events it publishes
		// through `events` reach clients together, so a pulled snapshot (TopicFrom) that reads that state sees all of
		// it or none. `change` is quick and calls nothing of the API but `events`. Any thread.
		public void Atomically(func<void(Events& events)> change);
		public void RemoveTopic(Core::StringView key);
		// Text for the output source `source` (one the manifest's outputSources declares) of workspace `wsId`. Any thread.
		public void Output(Core::StringView wsId, Core::StringView source, Core::StringView text);
		// The plugin's computed entries of a contribution point (docs/26 section 3) in workspace `wsId` ("": every
		// workspace): a JSON array of entries shaped as in the manifest, replacing those it gave before ("[]" removes
		// them); false with `problem` when `entries` is not an array of objects.
		public bool Contribute(Core::StringView point, Core::StringView wsId, Core::StringView entries, Core::String& problem);

		// Starts `body` on a thread of the plugin's own: the API's free functions (FindWorkspace, ReadFile, RunTask, ...)
		// know the calling plugin on it, as on the plugin's thread and in its methods. A script stops its threads in
		// OnDeactivate.
		public void StartThread(func<void()> body);

		// The server's record of the plugin, for the API's own classes (HostLock).
		public void* Handle() const { return host; }

		private void* host;
	}

	// Holds the server's lock for a scope (docs/26 "Built-ins on the API"): state a plugin keeps consistent with its
	// topics' pulled snapshots and events changes under it, as the server's own services' state does. The API's calls
	// work inside it - they do not take the lock again - and Wait releases it while it waits. Nothing slow inside it.
	class HostLock
	{
		[operator("create")] public void Init(const PluginContext& context);
		[operator("delete")] public void Delete();

		// Waits on `condition` (notified under the lock) with the lock released, at most `ns` nanoseconds (negative:
		// until notified).
		public void Wait(Threading::ConditionVariable& condition, int64 ns);

		private void* host;
		private bool taken;
	}
}
