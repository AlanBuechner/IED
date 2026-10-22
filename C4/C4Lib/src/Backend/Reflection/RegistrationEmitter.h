#pragma once
#include "Core/Core.h"
#include "Compiler/ErrorTracker.h"
#include "Backend/Reflection/ImageDescription.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm { class Function; class GlobalVariable; }

namespace Scripting
{
	class ModuleContext;
	class ProgramModel;
	struct FunctionRecord;

	/// How reflection calls a function (D-50): through its invoke thunk, which for a thunk shared by its signature calls
	/// `code` (null for a thunk of its own).
	struct InvokeEntry
	{
		llvm::Function* thunk = nullptr;
		llvm::Function* code = nullptr;
		llvm::Function* function = nullptr;   // the reflected function itself, whose address reflection records
	};

	/// The functions reflection can call, each with its thunk.
	using InvokeThunks = std::unordered_map<const FunctionRecord*, InvokeEntry>;

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

	/// The symbol of a function's own invoke thunk: `__c4_invoke_` plus the function's symbol, escaped
	/// (`QualifiedName.h` EscapedText).
	std::string InvokeThunkSymbol(const FunctionRecord& function);

	/// The symbol of the invoke thunk shared by every function of `function`'s signature: `__c4_invoke_sig_` plus its
	/// InvokeSignature, escaped.
	std::string SharedInvokeThunkSymbol(const FunctionRecord& function);

	/// The program's reflection image (docs/runtime/02, ABI in C4Runtime/ReflectABI.h) as the model describes it:
	/// tables of its types, enums, unions, globals and functions in one `c4_reflect_image`. A function in `thunks`
	/// records its invoke thunk, and the code a shared thunk calls; a global, its storage in `module`. What `host`
	/// registered is left out (a script's image holds only the script's own). An annotation argument that does not
	/// fold is reported in `errors` once. Empty when there is nothing to register. It reads the model, so it runs where
	/// the model may be read; ReflectionTables builds it anywhere (D-82).
	ImageDescription DescribeReflectionImage(ModuleContext& module, const ProgramModel& model, const InvokeThunks& thunks,
		const HostEntities& host, ErrorTracker& errors);

	/// The qualified names of the types, enums and unions the program's image registers, in its order.
	std::vector<std::string> ReflectedTypeNames(const ProgramModel& model);
} // namespace Scripting
