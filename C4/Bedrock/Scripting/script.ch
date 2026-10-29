// Scripts (docs/runtime/04): a Host opens the program's interface, compiles scripts from files or text, and a Script
// is one loaded script: its functions and types are found through reflection under its tag, calls into it are
// guarded, and Unload ends it. Over C4Script.dll's C API (C4Script/src/C4Script.h).
include "Container/array.ch"
include "Text/string.ch"
include "Reflection/reflect.ch"

// The program's symbol table: the compiler builds it into a program built with --script-host.
[link("__c4_script_table")] uint8 __c4_script_table;

// C4Script's c4s_options, field for field.
struct __C4sOptions
{
	public const char* name;
	public const char** paths;
	public const char** texts;
	public uint32 fileCount;
	public const char** allow;
	public uint32 allowCount;
	public const char** defines;
	public uint32 defineCount;
	public int32 optimize;
}

[link("c4s_host_open")] void* c4s_host_open(const char* interfacePath, void* table, char* error, uint64 errorSize);
[link("c4s_host_close")] void c4s_host_close(void* host);
[link("c4s_host_use_cache")] int32 c4s_host_use_cache(void* host, const char* folder);
[link("c4s_host_debug_scripts")] int32 c4s_host_debug_scripts(void* host, int32 on);
[link("c4s_compile")] void* c4s_compile(void* host, __C4sOptions* options);
[link("c4s_reload")] void* c4s_reload(void* host, void* old, __C4sOptions* options);
[link("c4s_prepare")] void* c4s_prepare(void* host, __C4sOptions* options);
[link("c4s_script_prepared")] int32 c4s_script_prepared(void* script);
[link("c4s_script_load")] int32 c4s_script_load(void* script);
[link("c4s_script_ok")] int32 c4s_script_ok(void* script);
[link("c4s_script_diagnostics")] const char* c4s_script_diagnostics(void* script);
[link("c4s_script_tag")] const char* c4s_script_tag(void* script);
[link("c4s_script_cached")] int32 c4s_script_cached(void* script);
[link("c4s_script_faulted")] int32 c4s_script_faulted(void* script);
[link("c4s_script_fault")] const char* c4s_script_fault(void* script);
[link("c4s_script_find")] void* c4s_script_find(void* script, const char* symbol);
[link("c4s_script_call")] int32 c4s_script_call(void* script, funcptr<void(void*)> fn, void* context);
[link("c4s_script_unload")] uint64 c4s_script_unload(void* script);
[link("c4s_script_destroy")] void c4s_script_destroy(void* script);
[link("c4s_guarded_call")] int32 c4s_guarded_call(funcptr<void(void*)> fn, void* context, void* fault);

namespace Scripting
{
	// What stopped a guarded call: the exception code (an access violation is 0xC0000005), the instruction, and for
	// an access violation whether it wrote (1), executed (8) or read (0), and the address it touched.
	struct Fault
	{
		public uint32 code;
		public uint32 write;
		public uint64 address;
		public uint64 target;
	}

	// Runs fn(context); a hardware fault in it stops the call: false, with the fault. A C4 exception passes through.
	bool Guard(funcptr<void(void*)> fn, void* context, Fault* fault);

	// What a script is compiled from (docs/runtime/04 section 1).
	class ScriptOptions
	{
	public:
		public Core::String name = Core::String("script"); // the script's name, also its reflection tag
		public bool optimize = true;                         // -O2; false: -O0
		// A file on disk.
		public void AddFile(const char* path);
		// A file given as text, under `name` (another file of the script includes it by that name).
		public void AddText(const char* fileName, const char* text);
		// A host project or interface path the script may include (none: every host header).
		public void Allow(const char* projectOrPath);
		// NAME or NAME=VALUE, as after -D.
		public void Define(const char* define);

		public Core::Array<Core::String> paths;
		public Core::Array<Core::String> texts;
		public Core::Array<bool> given;
		public Core::Array<Core::String> allowed;
		public Core::Array<Core::String> defines;
	}

	// One compiled script. Unload ends it; a Script that is collected unloads first.
	class Script
	{
	public:
		[operator("create")] public void Init(void* compiled, const ScriptOptions& from) { handle = compiled; options = from; }
		[operator("delete")] public void Destroy();

		// What it was compiled from (Host::Reload compiles it again).
		public const ScriptOptions& Options() const { return options; }
		// Its C4Script handle (C4Script/src/C4Script.h).
		public void* Handle() const { return handle; }

		// It compiled, linked and started, and no guarded call into it faulted.
		public bool Ok() const { return c4s_script_ok(handle) != 0; }
		// Host::Prepare compiled and linked it, and it waits for Load.
		public bool Prepared() const { return c4s_script_prepared(handle) != 0; }
		// Loads what Host::Prepare made (its reflection, roots and globals): false, with nothing of it loaded and the
		// diagnostics saying why, when its globals' start fails or it is not prepared.
		public bool Load() { return c4s_script_load(handle) != 0; }
		public bool Faulted() const { return c4s_script_faulted(handle) != 0; }
		public const char* Diagnostics() const { return c4s_script_diagnostics(handle); }
		public const char* Tag() const { return c4s_script_tag(handle); }
		// It was linked from the host's cache (Host::UseCache), not compiled.
		public bool Cached() const { return c4s_script_cached(handle) != 0; }
		// What stopped the call that faulted ("" when none did).
		public const char* FaultText() const { return c4s_script_fault(handle); }

		// The address of the script's symbol (Reflection::Function.Symbol()), or null.
		public void* Find(const char* symbol) const { return c4s_script_find(handle, symbol); }
		// The script's own free function or type of that qualified name (`Enemy::Spawn`), from reflection: invalid
		// while a script registered after it holds the name (reflection shows the latest registration).
		public Reflection::Function FindFunction(const char* name) const;
		public Reflection::Type FindType(const char* name) const;
		// The address of the script's free function `name`, for a funcptr, whatever registered the name after it;
		// null when it has none.
		public void* Address(const char* name) const;

		// Runs fn(context) guarded: false when it faulted, which marks the script faulted; a faulted script refuses
		// further calls.
		public bool Call(funcptr<void(void*)> fn, void* context) { return c4s_script_call(handle, fn, context) != 0; }
		// Calls a function of the script through reflection, guarded; false when it faulted or the arguments do not fit.
		template<...Ts> public bool Invoke(const Reflection::Function& function, void* result, const Ts&... args)
		{
			if (sizeof...(Ts) != function.ParamCount()) return false;
			void*[sizeof...(Ts) + 1] addresses = { (void*)&args..., nullptr };
			return InvokeWith(function, result, &addresses[0]);
		}
		public bool InvokeWith(const Reflection::Function& function, void* result, void** arguments);

		// Runs the script's [test] functions with the test harness (docs/runtime/03), guarded: those `filter` matches
		// (a glob; null: every one). The number of failed assertions, or -1 when it has no tests or a test faulted.
		public int32 RunTests(const char* filter);

		// Ends the script (docs/runtime/04 section 5): its exits run, its thread storage, roots, reflection entries and
		// code go. Returns how many finalizers of its objects still alive it dropped.
		public int64 Unload() { return (int64)c4s_script_unload(handle); }

	private:
		private void* handle = nullptr;
		private ScriptOptions options;
	}

	// The program's side of scripting: its interface, from which it compiles scripts.
	class Host
	{
	public:
		// The interface beside the program (<the executable>.c4i), or at `path`.
		public static Host* Open();
		public static Host* OpenAt(const char* path);
		[operator("delete")] public void Close();

		public bool IsOpen() const { return handle != nullptr; }
		// Why it did not open.
		public const char* Error() const { return error.CStr(); }
		// Keeps compiled scripts in `folder` from now on (docs/runtime/04 section 4.6): a script compiled again with the
		// same files, options, host build and compiler links the machine code kept from before. False when it cannot.
		public bool UseCache(const char* folder) { return c4s_host_use_cache(handle, folder) != 0; }
		// Whether scripts are debugged (docs/runtime/04 section 6.4): each linked into a DLL with a PDB, which debuggers
		// read as any module's, instead of in memory. A host built with -g starts with it on.
		public bool DebugScripts() const { return c4s_host_debug_scripts(handle, -1) != 0; }
		public void SetDebugScripts(bool on) { c4s_host_debug_scripts(handle, on ? 1 : 0); }
		// Compiles, links and loads a script; Ok() and Diagnostics() say how it went.
		public Script* Compile(const ScriptOptions& options);
		// Compile without the load (docs/runtime/04 section 5.1): Prepared() and Diagnostics() say how it went, and
		// Script::Load loads it - compile on a worker, load where nothing reads the reflection a load changes.
		public Script* Prepare(const ScriptOptions& options);
		// Compiles `script`'s options again to replace it (docs/runtime/04 section 5): when the new one compiles, `script`
		// unloads and the new one loads; when it does not, `script` stays loaded and the new one has the diagnostics.
		public Script* Reload(Script* script);

	private:
		private void* handle = nullptr;
		private Core::String error;
	}
}
