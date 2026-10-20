#pragma once
#include "Core/Core.h"
#include <string>
#include <unordered_set>

namespace llvm { class Module; }

namespace Scripting
{
	/// The symbol of a host thread_local's accessor (Script/HostTable.h): `__c4_tls_<symbol>`.
	std::string ThreadLocalAccessor(const std::string& symbol);

	/// The host's definitions win in a script's module (docs/runtime/04 section 4.2): a function the host table names
	/// keeps its body only for inlining (`available_externally`), and a global it names becomes an external
	/// declaration of the host's storage. Names follow from the program, so such a definition is the host's own code
	/// (a template instance the host made, a lambda in it, a compiler-written function).
	void UseHostDefinitions(llvm::Module& module, const std::unordered_set<std::string>& hostSymbols);

	/// A host thread_local is reached through its accessor: script code cannot address the host's native thread
	/// storage. Every use of a thread_local whose accessor the host table names calls the accessor instead, and the
	/// variable goes.
	void UseHostThreadStorage(llvm::Module& module, const std::unordered_set<std::string>& hostSymbols);

	/// Script code destroys its objects through the script's exit lists and throws with its image's base
	/// (C4Runtime/ScriptRuntimeC.h): `c4_at_exit`, `c4_thread_at_exit` and `_CxxThrowException` are redirected, and
	/// `__c4_script_module` holds the script's module record, which the loader stores before anything runs. Running it
	/// again (after the host's inline bodies come in) redirects what they named too.
	void UseScriptRuntime(llvm::Module& module);

	/// The global the loader stores the script's c4_script_module in.
	inline constexpr const char* kScriptModuleSymbol = "__c4_script_module";
} // namespace Scripting
