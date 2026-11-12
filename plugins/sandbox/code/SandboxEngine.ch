// The engine sandboxes run on (docs/34 "The engine"): Docker's command-line program (sandbox.docker), whether its
// engine answers, what it is and which platforms it runs - natively or under QEMU emulation - and starting Docker
// Desktop or installing emulators when asked. Any thread: a look runs docker outside the lock and caches its answer.

include "api/Plugin.ch"
include "api/Process.ch"

namespace Sandbox
{
	class EngineInfo
	{
		public Core::String m_State;                    // "missing", "stopped", "starting", "ready" or "failed"
		public Core::String m_Program;                  // the docker program found
		public Core::String m_Error;                    // why it is not ready, as Docker said it
		public Core::String m_ClientVersion;
		public Core::String m_ServerVersion;
		public Core::String m_Product;                  // "Docker Desktop 4.91.0 (239619)"
		public Core::String m_Os;                       // the engine's: "linux" or "windows"
		public Core::String m_Arch;                     // "amd64", "arm64"
		public int64 m_Cpus;
		public int64 m_MemoryBytes;
		public Core::Array<Core::String> m_Platforms;   // native first, then emulated
		public int64 m_CheckedMs;
	}

	class SandboxEngine
	{
		[operator("create")] public void Init(IED::PluginContext* context) { m_Context = context; }

		// The engine as it is now: looked at again when `force`, when the last look is older than kEngineCacheMs or when
		// it was not ready.
		public EngineInfo Look(bool force);
		// The last look, without running anything.
		public EngineInfo Last();
		// Starts Docker Desktop and waits for its engine (at most kEngineStartMs, or until `cancelled`); false with
		// `problem`. Its progress is the state "starting" meanwhile.
		public bool StartEngine(const bool* cancelled, Core::String& problem);
		// Registers QEMU's handlers for every platform in the engine (a privileged container of tonistiigi/binfmt); its
		// output in `output`. False when it failed.
		public bool InstallEmulators(Core::String& output);

		// Runs docker with `args` to its end (at most `timeoutMs`); its stdout in output, stderr in errors.
		public Process::Output Run(const Core::Array<Core::String>& args, int32 timeoutMs);
		// Starts docker with `args`, stdout and stderr piped apart, stdin from nowhere; a closed Child with its Error()
		// when docker is not there.
		public Process::Child Start(const Core::Array<Core::String>& args);
		// The docker program and its leading arguments (sandbox.docker); empty program when it is not found, with why.
		public bool Command(Core::String& program, Core::Array<Core::String>& leading, Core::String& problem);

		private Process::StartInfo Info(const Core::Array<Core::String>& args, bool& found);
		private void Store(const EngineInfo& info);

		private IED::PluginContext* m_Context;
		private Threading::Mutex m_Lock;
		private EngineInfo m_Info;                      // under m_Lock
		private bool m_Starting;                        // under m_Lock
		private Core::String m_PlatformsOf;             // the server version m_Platforms was read for; under m_Lock
		private Core::Array<Core::String> m_Platforms;  // under m_Lock
		private int64 m_Cpus;                           // under m_Lock
		private int64 m_MemoryBytes;                    // under m_Lock
	}

	// The engine as JSON: {state, program, error, clientVersion, serverVersion, product, os, arch, cpus, memoryBytes,
	// platforms: [{platform, native}], checkedMs}.
	Core::String EngineJson(const EngineInfo& info);
	// The platform the engine runs natively ("linux/amd64").
	Core::String NativePlatform(const EngineInfo& info);
	// Whether `platform` runs without emulation on an engine whose own platform is `native`: itself, and 32-bit x86 on
	// 64-bit x86.
	bool RunsNatively(Core::StringView platform, Core::StringView native);
	// What to do about an engine that is not ready, for the owner and the AI: one or two sentences.
	Core::String EngineAdvice(const EngineInfo& info);
	// The platforms in `docker buildx inspect`'s text ("Platforms: linux/amd64, linux/arm64, ..."), normal and once each.
	Core::Array<Core::String> PlatformsOfBuildx(Core::StringView text);

	int64 UnixMs();

	const int64 c_EngineCacheMs = 15000;
	const int64 c_EngineStartMs = 180000;
	const char* c_DockerSetting = "sandbox.docker";
}
