#pragma once
#include "Script/ScriptJit.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm
{
	class LLVMContext;
	class Module;
	class TargetMachine;
}

namespace Scripting
{
	/// How a debug script reaches one name it takes from outside (docs/runtime/04 section 6.4).
	enum class ImportKind
	{
		Slot,  // its code loads `__imp_<name>`, a slot the binding fills with the address
		Thunk, // its code calls the name, or its unwind tables name it: a thunk in the image jumps through a slot
		Data,  // only its data names it (a vtable, a table of pointers): a stand-in whose address the binding replaces
	};

	struct ScriptImport
	{
		std::string name;
		ImportKind kind = ImportKind::Slot;
		void* address = nullptr;
	};

	/// The names a script's object defines, which its module exports.
	std::optional<std::vector<std::string>> ScriptDefinitions(const std::string& object, std::string* error);

	/// What a script's object takes from outside, each resolved now (ResolveScriptSymbol): empty, naming what nothing
	/// defines, when one is not.
	std::optional<std::vector<ScriptImport>> ScriptImports(const std::string& object, const HostSymbolTable& host, std::string* error);

	/// The module of the import object linked beside the script: a slot per import (`__imp_<name>` naming a slot's),
	/// the thunks and their table, the data stand-ins, and the floating-point marker.
	std::unique_ptr<llvm::Module> ScriptImportModule(llvm::LLVMContext& context, const std::vector<ScriptImport>& imports,
		const llvm::TargetMachine& machine);

	/// Points the loaded module at what it imports, before any of its code runs: fills the slots, and turns each address
	/// its loader wrote that names a thunk or a stand-in into the host's own, at the same offset. False, with the reason,
	/// when the module lacks its import tables.
	bool BindScriptImports(void* module, const std::vector<ScriptImport>& imports, std::string* error);
} // namespace Scripting
