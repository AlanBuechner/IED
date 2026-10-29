#pragma once
#include "Core/Core.h"
#include "Script/HostInterface.h"
#include "Script/ScriptCache.h"
#include "Script/ScriptCompiler.h"
#include "Script/ScriptJit.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Scripting
{
	/// What stopped a guarded call (C4Runtime's c4_script_fault, docs/runtime/04 section 7).
	struct ScriptFault
	{
		uint32 code = 0;     // the exception code (an access violation is 0xC0000005)
		uint32 write = 0;    // an access violation: 1 for a write, 8 for an execution, 0 for a read
		uint64 address = 0;  // the faulting instruction
		uint64 target = 0;   // an access violation: the address it touched
	};

	/// Runs fn(context) and stops it at a hardware fault: false, with the fault. C++ and C4 exceptions pass through.
	using GuardedCall = bool (*)(void (*fn)(void*), void* context, ScriptFault* fault);

	/// The host runtime's script support (C4Runtime/ScriptRuntimeC.h and the GC and reflection), from its table.
	struct HostRuntime
	{
		void* (*moduleCreate)() = nullptr;
		void (*moduleFinish)(void*) = nullptr;
		void (*moduleDestroy)(void*) = nullptr;
		void (*tlsRelease)(void* const*, uint32) = nullptr;
		void (*addRootTable)(const void*) = nullptr;
		void (*removeRootTable)(const void*) = nullptr;
		uint64 (*dropFinalizersIn)(const void*, const void*) = nullptr;
		void (*forgetTypesIn)(const void*, const void*) = nullptr;
		void (*registerImage)(const char*, const void*) = nullptr;
		void (*notifyUnload)(const char*) = nullptr;
		uint32 (*removeSource)(const char*) = nullptr;

		/// Each one from `table`; empty, naming the first it lacks, when the host has not one of them.
		static std::optional<HostRuntime> From(const HostSymbolTable& table, std::string* error);
	};

	/// A compiled and loaded script (docs/runtime/04 section 4, step 7), or the diagnostics of one that did not get so
	/// far.
	class Script
	{
	public:
		~Script();
		Script(const Script&) = delete;
		Script& operator=(const Script&) = delete;

		/// It compiled, linked and started, and no guarded call into it faulted.
		bool Ok() const { return m_Linked && !Faulted(); }
		/// ScriptHost::Prepare compiled and linked it, and it waits for LoadPrepared.
		bool Prepared() const { return m_Prepared != nullptr; }
		/// Loads what Prepare compiled and linked (section 4, step 7): its reflection, roots and globals. False, with nothing
		/// of it loaded and the diagnostics saying why, when its start fails or it is not prepared.
		bool LoadPrepared();
		bool Faulted() const { return m_Faulted.load(std::memory_order_acquire); }
		const std::string& Diagnostics() const { return m_Diagnostics; }
		/// It was linked from the host's script cache (section 4.6), not compiled.
		bool Cached() const { return m_Cached; }
		const std::string& Tag() const { return m_Tag; }
		/// What stopped the call that faulted first: the code, the instruction (as an offset into the script when it
		/// is one of its) and what it touched; "" while none did. Written once, so any thread may read it while calls
		/// on other threads fault (BUG-396).
		const std::string& FaultText() const;

		/// The address of the script's symbol `name`; null when it has none or is not loaded.
		void* Find(const std::string& name) const;
		/// Runs fn(context) guarded; a fault marks the script faulted, and a faulted script refuses further calls.
		bool Call(void (*fn)(void*), void* context);
		/// Runs the script's exits, frees its thread storage, roots, reflection entries and code (section 5). Returns
		/// how many finalizers of objects still alive it dropped. Nothing of it may run afterwards.
		uint64 Unload();

	private:
		friend class ScriptHost;
		Script(const HostRuntime& runtime, GuardedCall guard, std::shared_ptr<std::recursive_mutex> loading, std::string tag)
			: m_Runtime(runtime), m_Guard(guard), m_Loading(std::move(loading)), m_Tag(std::move(tag)) {}
		bool Load(std::unique_ptr<LinkedScript> linked, const std::vector<std::string>& threadLocals);
		bool CallStart(void (*start)());
		/// The script faulted: the first fault, of the calls that fault on several threads at once, is the one kept.
		void RecordFault(const ScriptFault& fault);
		/// The script stops for a reason that is no hardware fault (an exception escaped its start or its exits).
		void MarkFaulted(const char* why);
		std::string DescribeFault() const;

		HostRuntime m_Runtime;              // a copy: a script may outlive its host object
		GuardedCall m_Guard;
		std::shared_ptr<std::recursive_mutex> m_Loading; // the host's: one load or unload at a time
		std::string m_Tag;
		std::string m_Diagnostics;
		std::unique_ptr<LinkedScript> m_Linked;
		std::unique_ptr<LinkedScript> m_Prepared;         // linked by Prepare, until LoadPrepared
		std::vector<std::string> m_PreparedThreadLocals;
		ScriptImage m_Image;               // where its code was, for the fault text after it unloads
		void* m_Record = nullptr;          // the script's c4_script_module
		const void* m_Roots = nullptr;     // its __c4_gc_roots
		std::vector<void*> m_TlsControls;  // its thread_locals' __emutls_v controls
		std::atomic<bool> m_Faulted{ false };
		bool m_Cached = false;
		std::mutex m_FaultLock;            // the first fault's record, written once under it
		ScriptFault m_Fault;
		std::string m_FaultPlace;          // the fault's module and function, when the script's code has symbols
		std::string m_FaultText;
	};

	/// A program's side of scripting: its interface and table, from which it compiles and loads scripts.
	class ScriptHost
	{
	public:
		/// Opens the interface at `interfacePath` for the program whose table is `table` (`&__c4_script_table`).
		static std::unique_ptr<ScriptHost> Open(const std::string& interfacePath, const void* table, GuardedCall guard, std::string* error);

		/// Compiles, links and loads a script; the result says what went wrong when it did. Several may compile at once
		/// on different threads (each with a target machine of its own).
		std::unique_ptr<Script> Compile(const ScriptCompileOptions& options);
		/// Compile without the load: the script compiled and linked (Prepared()), for Script::LoadPrepared to load later -
		/// a host compiles on a worker and loads where nothing reads the reflection the load changes (between frames).
		std::unique_ptr<Script> Prepare(const ScriptCompileOptions& options);
		/// Compiles `options` to replace `old` (section 5): when it compiles and links, `old` unloads, then it loads;
		/// when it does not, `old` stays loaded and the result has the diagnostics.
		std::unique_ptr<Script> Reload(Script& old, const ScriptCompileOptions& options);

		/// Keeps compiled scripts in `folder` from now on (section 4.6): a compile whose inputs are an earlier one's
		/// links that compile's machine code. False, with nothing kept, when the compiler's own file cannot be named.
		bool UseCache(const std::string& folder);

		/// Whether scripts are debugged (section 6.4): each linked into a DLL with a PDB that debuggers read, instead of
		/// in memory. On at first when the host was built with -g.
		bool DebugScripts() const { return m_DebugScripts; }
		void SetDebugScripts(bool on) { m_DebugScripts = on; }

		const HostInterface& Interface() const { return m_Interface; }

		~ScriptHost();

	private:
		ScriptHost(HostInterface host, HostSymbolTable table, HostRuntime runtime, GuardedCall guard);

		/// A script compiled and linked but not loaded, and what loading it takes.
		struct Built
		{
			std::unique_ptr<Script> script;
			std::unique_ptr<LinkedScript> linked; // null when it did not compile or link
			std::vector<std::string> threadLocals;
		};
		Built Build(const ScriptCompileOptions& options);
		Built NewBuilt(const ScriptCompileOptions& options) const;
		/// Links an earlier compile's machine code; nothing linked when it does not link.
		Built LinkCached(const ScriptCompileOptions& options, CachedScript cached);
		/// Compiles and links; `generated`, when given, gets the machine code.
		Built CompileAndLink(const ScriptCompileOptions& options, std::string* generated);
		static void LoadOrUndo(Script& script, std::unique_ptr<LinkedScript> linked, const std::vector<std::string>& threadLocals);

		/// Links a compiled script: into a DLL when scripts are debugged, else in memory.
		std::unique_ptr<LinkedScript> Link(ScriptCode code, const std::string& name, std::string* generated, std::string* error);

		HostInterface m_Interface;
		HostIndex m_Index;
		HostSymbolTable m_Table;
		HostRuntime m_Runtime;
		GuardedCall m_Guard;
		std::optional<ScriptCache> m_Cache;
		std::atomic<bool> m_DebugScripts = false;
		std::atomic<uint64> m_DllLoads = 0;  // each debug script's module is a file of its own
		std::string m_DllRoot;               // where their process folders go (section 6.4)
		std::string m_DllFolder;             // this process's, made at the first debug link
		std::once_flag m_DllFolderMade;
		std::shared_ptr<std::recursive_mutex> m_Loading = std::make_shared<std::recursive_mutex>(); // an exit may compile
	};
} // namespace Scripting
