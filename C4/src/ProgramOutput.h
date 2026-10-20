#pragma once
#include "Compiler/BuildUtils.h"
#include "Compiler/CompilationResult.h"
#include "Compiler/CompilerInstance.h"
#include "Interop/InteropPass.h"
#include "Macro/MacroTable.h"
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace llvm { class Module; }

/// The last stage of a build (docs/tools/01-compiler-cli.md): the finished module as IR (`-S`), bitcode (`-bc`) or an
/// object the compiler optimizes and assembles itself, then linked with the C4 runtime into the program. A script host
/// also gets its symbol table, its `.c4i` interface and C4Script.dll beside it.
class ProgramOutput
{
public:
	/// What the earlier stages produced that the output needs.
	struct Inputs
	{
		const Scripting::CompileOptions& options;
		Scripting::CompilationResult& result;
		Scripting::CompilerInstance& compiler;
		const Scripting::InteropPass::Result& interop;
		const std::unordered_map<std::string, Scripting::MacroTable>& exportedMacros;
	};

	explicit ProgramOutput(const Inputs& inputs) : m_In(inputs) {}

	bool Write(llvm::Module& module);

private:
	bool MakeOutputFolder();
	std::string IntermediateBase();
	bool WriteIR(const llvm::Module& module, const std::string& path);
	bool WriteBitcode(const llvm::Module& module, const std::string& path);
	bool WriteObject(llvm::Module& module, const std::string& objPath);
	bool Link(const std::string& objPath);
	bool PrepareScriptHost(llvm::Module& module);
	bool ShipScriptHost(const std::filesystem::path& scriptHost);
	void Timed(const char* phase, std::chrono::steady_clock::time_point since);

	Inputs m_In;
};
