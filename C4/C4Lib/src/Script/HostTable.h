#pragma once
#include "Core/Core.h"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm
{
	class LLVMContext;
	class Module;
}

namespace Scripting
{
	/// The runtime entry points script code may call although its host never did (docs/runtime/04 section 3.1):
	/// the table lists them whatever the host uses.
	const std::vector<const char*>& ScriptRuntimeEntryPoints();

	/// An accessor `__c4_tls_<symbol>` for every thread_local global of `module`: it returns the calling thread's
	/// address of the variable, so a script, which cannot reach the host's native thread storage, names it.
	void EmitThreadLocalAccessors(llvm::Module& module);

	/// What a script may name in a module Lower finished: every function it defines, every global with storage of
	/// its own (not the private constants: a script makes its own), every declaration its code uses (the runtime,
	/// C functions), ScriptRuntimeEntryPoints and `alsoListed` (the C functions the prelude declares and what the
	/// host's foreign objects define, which the host links whether or not it uses them). Not thread_locals (their accessors instead), intrinsics, or the
	/// module's own tables (reflection image, GC roots). In module order, deterministic.
	std::vector<std::string> HostSymbols(llvm::Module& module, const std::vector<std::string>& alsoListed);

	/// A name the table of a program emitted in partitions lists (D-81): the name scripts know (ProgramName), and what
	/// the table's own module declares to take its address: the partition's symbol, a function or data, hidden for a
	/// local of the program.
	struct HostEntry
	{
		std::string name;
		std::string symbol;
		bool function = true;
		bool hidden = false;
	};

	/// HostSymbols of a program emitted in partitions: every partition's in partition order, then the runtime entry
	/// points and `alsoListed` none of them declares, each name once. The table is a module of its own
	/// (HostTableModule), so no partition changes with what another holds: partition 0's own locals (the shared vtables
	/// of a host among them) become hidden globals there, as the program's locals are, for the table to reach; another
	/// partition's own locals (a census counter, a vtable's copy) are its helpers and are not listed.
	std::vector<HostEntry> HostEntries(const std::vector<llvm::Module*>& partitions, const std::vector<std::string>& alsoListed);

	/// The names of `entries`, in order, as the interface lists them.
	std::vector<std::string> HostNamesOf(const std::vector<HostEntry>& entries);

	/// The table of a program emitted in partitions (EmitHostTable) in a module of its own in `context`, for the
	/// target `triple` and data `layout`, declaring each entry. It reads no partition: any thread may build it.
	std::unique_ptr<llvm::Module> HostTableModule(llvm::LLVMContext& context, const std::vector<HostEntry>& entries,
		const std::string& triple, const std::string& layout);

	/// What a HostTableModule for `target` (its triple and data layout) is made of, as 32 hex digits: the key its
	/// object is kept by.
	std::string HostTableKey(const std::vector<HostEntry>& entries, const std::string& target);

	/// The external functions and globals the host's C and C++ objects define (`extern "c" source`, binding shims):
	/// listed so a script reaches every binding the host imported, not only those its code calls. Empty, naming the
	/// object in `error`, when one cannot be read.
	std::optional<std::vector<std::string>> ForeignDefinitions(const std::vector<std::string>& objectPaths, std::string* error);

	/// `__c4_script_table = { &__c4_script_build_id, count, address[count] }`, address i being `symbols[i]`, external
	/// and constant. It replaces the declaration the scripting API's [link] made. Every entry is referenced, so no
	/// listed function or global is dropped by the optimizer or the linker. The build id, which covers the inline
	/// bodies, is only declared here (EmitBuildId defines it), so the table does not wait for them. False, naming it in
	/// `error`, when a symbol is not in the module.
	bool EmitHostTable(llvm::Module& module, const std::vector<std::string>& symbols, std::string* error);

	/// `__c4_script_build_id`, the 16 bytes of the interface's build id (InterfaceBuilder), defined in `module`: the
	/// program's own when its inline bodies came before its code was split, else a module of its own linked with it.
	void EmitBuildId(llvm::Module& module, const std::array<uint8, 16>& buildId);
} // namespace Scripting
