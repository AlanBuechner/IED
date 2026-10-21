#include "Script/ScriptHost.h"
#include "Script/ScriptModulePasses.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <cstdio>

namespace Scripting
{
	namespace
	{
		template <typename F>
		bool Take(const HostSymbolTable& table, const char* name, F& slot, std::string* error)
		{
			slot = table.Function<F>(name);
			if (slot) return true;
			if (error) *error = "the program's script host table has no '" + std::string(name) + "' (its runtime is too old for this script host)";
			return false;
		}

		void RunStart(void* start) { reinterpret_cast<void (*)()>(start)(); }

		struct Finish
		{
			void (*finish)(void*);
			void* record;
		};

		void RunFinish(void* context)
		{
			const Finish* finish = static_cast<const Finish*>(context);
			finish->finish(finish->record);
		}

		std::string Hex(uint64 value)
		{
			char text[24];
			std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
			return text;
		}
	}

	std::optional<HostRuntime> HostRuntime::From(const HostSymbolTable& table, std::string* error)
	{
		HostRuntime runtime;
		const bool complete = Take(table, "c4_script_module_create", runtime.moduleCreate, error) &&
			Take(table, "c4_script_module_finish", runtime.moduleFinish, error) &&
			Take(table, "c4_script_module_destroy", runtime.moduleDestroy, error) &&
			Take(table, "c4_script_tls_release", runtime.tlsRelease, error) &&
			Take(table, "c4_gc_add_root_table", runtime.addRootTable, error) &&
			Take(table, "c4_gc_remove_root_table", runtime.removeRootTable, error) &&
			Take(table, "c4_gc_drop_finalizers_in", runtime.dropFinalizersIn, error) &&
			Take(table, "c4_reflect_register_image", runtime.registerImage, error) &&
			Take(table, "c4_reflect_notify_unload", runtime.notifyUnload, error) &&
			Take(table, "c4_reflect_remove_source", runtime.removeSource, error);
		if (!complete) return std::nullopt;
		return runtime;
	}

	// --- a script ----------------------------------------------------------------------------------------------------

	Script::~Script() { Unload(); }

	std::string Script::FaultText() const
	{
		if (!m_Faulted) return "";
		std::string text = "exception " + Hex(m_Fault.code);
		if (m_Fault.address >= m_Image.low && m_Fault.address < m_Image.high) text += " at script+" + Hex(m_Fault.address - m_Image.low);
		else text += " at " + Hex(m_Fault.address);
		if (m_Fault.code == 0xC0000005u)
			text += std::string(m_Fault.write == 1 ? ", writing " : m_Fault.write == 8 ? ", executing " : ", reading ") + Hex(m_Fault.target);
		return text;
	}

	void* Script::Find(const std::string& name) const { return m_Linked ? m_Linked->Find(name) : nullptr; }

	bool Script::Call(void (*fn)(void*), void* context)
	{
		if (!m_Linked || m_Faulted) return false;
		ScriptFault fault;
		if (m_Guard(fn, context, &fault)) return true;
		m_Faulted = true;
		m_Fault = fault;
		return false;
	}

	bool Script::CallStart(void (*start)())
	{
		try
		{
			if (Call(RunStart, reinterpret_cast<void*>(start))) return true;
			m_Diagnostics += "error: the script's globals faulted as they started: " + FaultText() + "\n";
		}
		catch (...)
		{
			m_Faulted = true;
			m_Diagnostics += "error: an exception escaped the start of the script's globals\n";
		}
		return false;
	}

	bool Script::Load(std::unique_ptr<LinkedScript> linked, const std::vector<std::string>& threadLocals)
	{
		std::lock_guard<std::recursive_mutex> loading(*m_Loading);
		m_Linked = std::move(linked);
		m_Image = m_Linked->Image();
		m_Record = m_Runtime.moduleCreate();
		*static_cast<void**>(m_Linked->Find(kScriptModuleSymbol)) = m_Record;
		if (const void* image = m_Linked->Find("__c4_reflect_image")) m_Runtime.registerImage(m_Tag.c_str(), image);
		m_Roots = m_Linked->Find("__c4_gc_roots");
		if (m_Roots) m_Runtime.addRootTable(m_Roots);
		for (const std::string& name : threadLocals)
			if (void* control = m_Linked->Find("__emutls_v." + name)) m_TlsControls.push_back(control);
		auto start = reinterpret_cast<void (*)()>(m_Linked->Find("__c4_script_init"));
		return !start || CallStart(start);
	}

	uint64 Script::Unload()
	{
		std::lock_guard<std::recursive_mutex> loading(*m_Loading);
		if (!m_Linked) return 0;
		m_Runtime.notifyUnload(m_Tag.c_str());
		Finish finish{ m_Runtime.moduleFinish, m_Record };
		try
		{
			ScriptFault fault;
			if (!m_Guard(RunFinish, &finish, &fault)) { m_Faulted = true; m_Fault = fault; }
		}
		catch (...)
		{
			m_Faulted = true; // a destructor threw; the rest still goes
		}
		if (!m_TlsControls.empty()) m_Runtime.tlsRelease(m_TlsControls.data(), static_cast<uint32>(m_TlsControls.size()));
		if (m_Roots) m_Runtime.removeRootTable(m_Roots);
		const ScriptImage image = m_Linked->Image();
		const uint64 dropped = m_Runtime.dropFinalizersIn(reinterpret_cast<const void*>(image.low), reinterpret_cast<const void*>(image.high));
		m_Runtime.removeSource(m_Tag.c_str());
		m_Runtime.moduleDestroy(m_Record);
		m_Linked.reset();
		m_Record = nullptr;
		m_Roots = nullptr;
		m_TlsControls.clear();
		return dropped;
	}

	// --- the host ------------------------------------------------------------------------------------------------------

	ScriptHost::ScriptHost(HostInterface host, HostSymbolTable table, HostRuntime runtime, GuardedCall guard)
		: m_Interface(std::move(host)), m_Index(m_Interface), m_Table(std::move(table)), m_Runtime(runtime), m_Guard(guard) {}

	std::unique_ptr<ScriptHost> ScriptHost::Open(const std::string& interfacePath, const void* table, GuardedCall guard, std::string* error)
	{
		HostInterface host;
		if (!ReadInterface(interfacePath, host, error)) return nullptr;
		std::optional<HostSymbolTable> symbols = HostSymbolTable::Read(host, table, error);
		if (!symbols) return nullptr;
		std::optional<HostRuntime> runtime = HostRuntime::From(*symbols, error);
		if (!runtime) return nullptr;
		return std::unique_ptr<ScriptHost>(new ScriptHost(std::move(host), std::move(*symbols), *runtime, guard));
	}

	bool ScriptHost::UseCache(const std::string& folder)
	{
		const std::string compiler = ScriptCompilerIdentity();
		if (compiler.empty()) return false;
		m_Cache.emplace(folder, compiler);
		return true;
	}

	ScriptHost::Built ScriptHost::Build(const ScriptCompileOptions& options)
	{
		const std::string entry = m_Cache ? m_Cache->EntryFor(m_Interface, options) : "";
		if (std::optional<CachedScript> cached = entry.empty() ? std::nullopt : m_Cache->Read(entry))
			if (Built built = LinkCached(options, std::move(*cached)); built.linked) return built;
		std::string generated;
		Built built = CompileAndLink(options, entry.empty() ? nullptr : &generated);
		if (built.linked && !generated.empty()) m_Cache->Write(entry, { std::move(generated), built.script->m_Diagnostics, built.threadLocals });
		return built;
	}

	ScriptHost::Built ScriptHost::NewBuilt(const ScriptCompileOptions& options) const
	{
		Built built;
		built.script.reset(new Script(m_Runtime, m_Guard, m_Loading, options.name));
		return built;
	}

	ScriptHost::Built ScriptHost::LinkCached(const ScriptCompileOptions& options, CachedScript cached)
	{
		Built built = NewBuilt(options);
		ScriptCode code;
		code.object = std::move(cached.object);
		std::string error;
		built.linked = LinkScript(std::move(code), m_Table, m_Index.target, nullptr, &error);
		built.script->m_Cached = true;
		built.script->m_Diagnostics = std::move(cached.diagnostics);
		built.threadLocals = std::move(cached.threadLocals);
		return built;
	}

	ScriptHost::Built ScriptHost::CompileAndLink(const ScriptCompileOptions& options, std::string* generated)
	{
		Built built = NewBuilt(options);
		std::string error;
		std::unique_ptr<llvm::TargetMachine> machine = ScriptTargetMachine(m_Index.target, &error);
		if (!machine)
		{
			built.script->m_Diagnostics = "error: " + error + "\n";
			return built;
		}
		CompiledScript compiled = CompileScript(m_Interface, m_Index, options, *machine);
		built.script->m_Diagnostics = compiled.diagnostics;
		if (!compiled.module) return built;
		ScriptCode code;
		code.context = std::move(compiled.context);
		code.module = std::move(compiled.module);
		code.optimize = options.optimize;
		built.linked = LinkScript(std::move(code), m_Table, m_Index.target, generated, &error);
		if (!built.linked) built.script->m_Diagnostics += "error: " + error + "\n";
		built.threadLocals = std::move(compiled.threadLocals);
		return built;
	}

	std::unique_ptr<Script> ScriptHost::Compile(const ScriptCompileOptions& options)
	{
		Built built = Build(options);
		if (built.linked) LoadOrUndo(*built.script, std::move(built.linked), built.threadLocals);
		return std::move(built.script);
	}

	void ScriptHost::LoadOrUndo(Script& script, std::unique_ptr<LinkedScript> linked, const std::vector<std::string>& threadLocals)
	{
		if (!script.Load(std::move(linked), threadLocals)) script.Unload(); // nothing of a script that failed stays loaded
	}

	std::unique_ptr<Script> ScriptHost::Reload(Script& old, const ScriptCompileOptions& options)
	{
		Built built = Build(options);
		if (!built.linked) return std::move(built.script);
		std::lock_guard<std::recursive_mutex> loading(*m_Loading); // nothing loads between the old going and the new
		old.Unload();
		LoadOrUndo(*built.script, std::move(built.linked), built.threadLocals);
		return std::move(built.script);
	}
} // namespace Scripting
