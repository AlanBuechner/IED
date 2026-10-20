#pragma once
#include "Core/Core.h"
#include "Compiler/ErrorTracker.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace llvm { class GlobalVariable; }

namespace Scripting
{
	class ModuleContext;
	class ProgramModel;
	struct FunctionRecord;

	/// The functions reflection can call: each has a thunk named by InvokeThunkSymbol (D-50).
	using InvokeThunks = std::unordered_set<const FunctionRecord*>;

	/// What a script's host registered already (docs/runtime/04): its symbols and its types' names. Null fields in a
	/// program's own build.
	struct HostEntities
	{
		const std::unordered_set<std::string>* symbols = nullptr;
		const std::unordered_set<std::string>* types = nullptr;
		uint32 interfaceUnits = 0; // units 1..interfaceUnits are the host's files: what they declare is the host's
	};

	/// Whether reflection registers `function`: the program's own functions, not builtins, runtime entry points
	/// (`c4_` names), functions local to a body or functions the compiler wrote (BUG-238).
	bool ReflectedFunction(const FunctionRecord& function);

	/// The symbol of a function's invoke thunk: `__c4_invoke_` plus the function's symbol, escaped
	/// (`QualifiedName.h` EscapedText).
	std::string InvokeThunkSymbol(const FunctionRecord& function);

	/// The program's reflection image (docs/runtime/02, ABI in C4Runtime/ReflectABI.h): constant tables of its
	/// types, enums, unions, globals and functions in one `c4_reflect_image`, the external constant
	/// `__c4_reflect_image`, ready for `c4_reflect_register_image`. A function in `thunks` records its invoke
	/// thunk. What `host` registered is left out (a script's image holds only the script's own). An annotation argument
	/// that does not fold is reported in `errors` once. Null when there is nothing to register.
	llvm::GlobalVariable* EmitReflectionImage(ModuleContext& module, const ProgramModel& model, const InvokeThunks& thunks,
		const HostEntities& host, ErrorTracker& errors);

	/// The qualified names of the types, enums and unions the program's image registers, in its order.
	std::vector<std::string> ReflectedTypeNames(const ProgramModel& model);
} // namespace Scripting
