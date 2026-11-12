// A workspace's sandboxes (docs/34): their definitions read from .ied/sandbox/, the images and containers Docker holds
// for them, the jobs that build, start, stop and reset them, the commands run in them and the last validation. What the
// Sandboxes window shows lives here, on the topic sandbox:<wsId>, so a closed browser loses nothing. State is under the
// server's lock (IED::HostLock), so the pulled snapshot sees a change and its event together; docker runs outside it,
// and a sandbox's own lock (m_Op) keeps two jobs from building or starting one sandbox at once.
//
// The snapshot (and each "changed" event): {wsId, root, key, engine, sandboxes: [...], runs: [...], validation}; each
// "output" event {runId, text} adds to a run's output.

include "SandboxEngine.ch"
include "SandboxDefinition.ch"
include "api/Plugin.ch"

namespace Sandbox
{
	class SandboxRun
	{
		public int64 m_Id;
		public Core::String m_Sandbox;
		public Core::String m_Kind;          // "run", "build", "start", "stop", "reset", "remove", "pull", "emulators", "engine"
		public Core::String m_Label;         // the command, or what the job does
		public bool m_Running;
		public int64 m_ExitCode;
		public Core::String m_Stopped;       // why it was stopped ("stopped", "ran out of time"); "" when it ended itself
		public Core::String m_Problem;       // why it could not run
		public Core::String m_Output;        // the end of what it printed, at most c_KeptOutputBytes
		public Core::String m_Pending;       // a line not finished yet, for the Output window
		public int64 m_StartedMs;
		public int64 m_EndedMs;
		public bool m_Cancel;
		public Core::String m_PidFile;
	}

	class SandboxState
	{
		[operator("create")] public void Init() { m_Op = new Threading::Mutex(); }

		public Core::String m_Name;
		public SandboxDefinition m_Definition;
		public SandboxPlan m_Plan;
		public bool m_Planned;
		public Core::String m_Problem;       // the definition or its plan is wrong
		public bool m_ImageBuilt;
		public Core::String m_Container;     // "absent", "running", "stopped" or "stale" (made from another config)
		public Core::String m_Busy;          // the job on it now
		public int64 m_LastUsedMs;
		public int64 m_LastRunId;
		public Threading::Mutex* m_Op;
	}

	class ValidationRow
	{
		public Core::String m_Sandbox;
		public Core::String m_Os;
		public Core::String m_Platform;
		public Core::String m_Command;
		public Core::String m_Result;        // "queued", "running", "pass", "fail", "error", "stopped", "skipped"
		public int64 m_ExitCode;
		public int64 m_Ms;
		public int64 m_RunId;
		public Core::String m_Tail;          // a failure's last lines, or why it could not run
	}

	class ValidationRecord
	{
		public int64 m_Id;
		public Core::String m_Command;       // "" when each sandbox ran its own `validate`
		public bool m_Running;
		public int64 m_StartedMs;
		public int64 m_EndedMs;
		public Core::Array<ValidationRow> m_Rows;
	}

	class WorkspaceSandboxes
	{
		public Core::String m_WsId;
		public Core::String m_Root;
		public Core::String m_Key;
		public Core::Array<SandboxState*> m_Sandboxes;
		public Core::Array<SandboxRun*> m_Runs;
		public ValidationRecord m_Validation;
		public Core::String m_DockerProblem;  // the last docker query's failure
	}

	// What a command run in a sandbox came to.
	class RunOutcome
	{
		public int64 m_RunId;
		public bool m_Ran;                    // the command started (else m_Problem)
		public int64 m_ExitCode;
		public Core::String m_Stopped;
		public Core::String m_Problem;
		public Core::String m_Output;         // its kept output
		public Core::String m_Prepared;       // what was done first: "built the image (2 m 3 s); started"
		public int64 m_Ms;
	}

	class SandboxService
	{
		[operator("create")] public void Init(IED::PluginContext* context) { m_Context = context; m_Engine = new SandboxEngine(context); }

		// Each workspace's topic, the file watcher, the idle stop, the methods and bindings.
		public void Start();
		public void Stop();

		public SandboxEngine* Engine() { return m_Engine; }
		public const Catalog& TheCatalog();
		// The catalog as JSON for sandbox.catalog and sandbox_catalog: systems with versions marked downloaded, presets,
		// and the engine's platforms.
		public Core::String CatalogJson(bool downloaded);

		// Reads the workspace's definitions again (and Docker's state of them when `docker`), then publishes.
		public void Scan(Core::StringView wsId, bool docker);
		// The snapshot of a workspace, as the topic gives it.
		public Core::String SnapshotJson(Core::StringView wsId);

		// Writes .ied/sandbox/<name>/sandbox.json; false with `problem` (an invalid name, a system or platform the catalog
		// or the engine lacks, a sandbox of that name when not `replace`). `notes` says what to know (presets missing).
		public bool Create(const IED::Workspace& ws, const SandboxDefinition& definition, bool replace, Core::String& notes, Core::String& problem);
		// The workspace's definition `name` as it is on disk; false with `problem`.
		public bool Definition(const IED::Workspace& ws, Core::StringView name, SandboxDefinition& definition, Core::String& problem);

		// A job on sandbox `name`: build, rebuild, start, stop, reset, remove or pull, run to its end on this thread.
		// False with `problem`; `said` tells what was done.
		public bool Control(const IED::Workspace& ws, Core::StringView name, Core::StringView action, const bool* cancelled, Core::String& said, Core::String& problem);
		// Runs `command` in sandbox `name` - its image built and the sandbox started first when needed - and waits for it,
		// at most `seconds` or until `cancelled`.
		public RunOutcome Exec(const IED::Workspace& ws, Core::StringView name, Core::StringView command, Core::StringView cwd, const Core::Array<Core::String>& env, int64 seconds, const bool* cancelled);
		// Runs `command` (or each sandbox's `validate`) in the sandboxes `names` (empty: all), sandbox.parallelRuns at a
		// time, and records the matrix.
		public ValidationRecord Validate(const IED::Workspace& ws, const Core::Array<Core::String>& names, Core::StringView command, int64 seconds, const bool* cancelled);
		// Stops run `runId` (its command inside the sandbox too); false when no such run is going.
		public bool StopRun(Core::StringView wsId, int64 runId);
		// Starts Docker Desktop, publishing the engine's state as it goes.
		public bool StartEngine(Core::StringView wsId, const bool* cancelled, Core::String& problem);
		public bool InstallEmulators(Core::StringView wsId, Core::String& output);
		// Downloads `image` for `platform` (a catalog version), its output as a run of no sandbox.
		public RunOutcome Pull(const IED::Workspace& ws, Core::StringView image, Core::StringView platform, const bool* cancelled);
		// Removes a downloaded image.
		public bool RemoveImage(Core::StringView image, Core::String& said);

		// Runs `body` on a thread of the plugin's own (a window's job answers at once).
		public void Background(func<void()> body);
		// The workspace's sandboxes, read from disk for a system-message section: "name (os, platform)".
		public Core::Array<Core::String> Summaries(const IED::Workspace& ws);

		private WorkspaceSandboxes* Of(Core::StringView wsId);
		private void Publish(Core::StringView wsId);
		private SandboxState* Find(WorkspaceSandboxes* sandboxes, Core::StringView name);
		private void ReadDefinitions(const IED::Workspace& ws);
		private void ReadDocker(const IED::Workspace& ws);
		private void ContributeProfiles(const IED::Workspace& ws);
		private bool Prepare(const IED::Workspace& ws, Core::StringView name, SandboxState*& state, Core::String& problem);
		private bool Ensure(const IED::Workspace& ws, SandboxState* state, bool start, bool rebuild, SandboxRun* run, const bool* cancelled, Core::String& prepared, Core::String& problem);
		private bool BuildImage(const IED::Workspace& ws, SandboxState* state, bool noCache, SandboxRun* run, const bool* cancelled, Core::String& problem);
		private bool StartContainer(const IED::Workspace& ws, SandboxState* state, SandboxRun* run, Core::String& problem);
		private SandboxRun* BeginRun(Core::StringView wsId, Core::StringView sandbox, const char* kind, Core::StringView label);
		private void EndRun(Core::StringView wsId, SandboxRun* run, int64 exitCode, Core::StringView stopped, Core::StringView problem);
		private void Append(Core::StringView wsId, SandboxRun* run, Core::StringView text);
		private int64 Stream(Core::StringView wsId, SandboxRun* run, const Core::Array<Core::String>& args, int64 deadlineMs, const bool* cancelled, Core::StringView container, Core::String& stopped);
		private Process::Output Docker(const Core::Array<Core::String>& args, int32 timeoutMs);
		private void SetBusy(Core::StringView wsId, SandboxState* state, Core::StringView busy);
		private void LoadValidation(const IED::Workspace& ws);
		private void SaveValidation(const IED::Workspace& ws);
		private void IdleStop();
		private int64 SettingInt(const char* key, Core::StringView wsId, int64 fallback);

		private IED::PluginContext* m_Context;
		private SandboxEngine* m_Engine;
		private Catalog* m_Catalog;                                                    // read once, with the owner's
		private Core::String m_CatalogOwner;                                           // the sandbox.images it was read with
		private Threading::Mutex m_CatalogLock;
		private Core::HashMap<Core::String, WorkspaceSandboxes*> m_Workspaces;      // under the server's lock
		private int64 m_NextRunId;                                                     // under the server's lock
		private bool m_Stopping;                                                       // under the server's lock
	}

	Core::String SandboxTopic(Core::StringView wsId);
	// A duration as people read it: "850 ms", "12 s", "3 m 12 s", "1 h 4 m".
	Core::String DurationText(int64 ms);
	// The last `lines` lines of `text`, at most `bytes`.
	Core::String TailLines(Core::StringView text, int64 lines, int64 bytes);
	// The container's state from `docker ps`'s State and the config label against the plan's: "running", "stopped",
	// "stale".
	Core::String ContainerState(Core::StringView state, Core::StringView config, Core::StringView wanted);

	const int64 c_KeptOutputBytes = 65536;
	const int64 c_KeptRuns = 12;
	const int64 c_SnapshotRunsWithOutput = 6;
	const char* c_OutputSource = "sandbox";
}
