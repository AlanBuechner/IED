#pragma once
#include "Backend/CodeGen/ModuleContext.h"
#include "Model/TestRegistry.h"
#include <llvm/IR/IRBuilder.h>
#include <functional>
#include <string>
#include <unordered_map>

namespace Scripting
{
	/// What a `-test` build's harness reports beyond the tests themselves.
	struct TestHarnessOptions
	{
		std::string jsonPath;                              // -test-json: where the summary is written; empty for none
		std::function<std::string(uint32 sourceId)> fileOf; // a test's source file, as failures print it
		std::string entry = "main";                         // the harness's name; a script's is kScriptTestsSymbol
		uint32 hostUnits = 0;                               // a script's: units 1..hostUnits are its host's, tests and all
	};

	/// A script's test harness (docs/runtime/04 section 2), which Script::RunTests calls.
	inline constexpr const char* kScriptTestsSymbol = "__c4_script_tests";

	/// The `-test` entry point (docs/runtime/03): `main(argc, argv)` (or the entry the options name) runs every test
	/// of the program's own, each named, located and timed and skipped when `--filter` excludes it, bracketed by the
	/// global and per-class fixtures, then prints the summary and returns the number of failed assertions.
	class TestHarnessEmitter
	{
	public:
		TestHarnessEmitter(ModuleContext& mctx, const TestRegistry& tests, const TestHarnessOptions& options);
		/// False, emitting nothing, when the program has no test of its own.
		bool Emit();

	private:
		bool Own(const FunctionRecord* function) const { return function && function->declUnit > m_Options.hostUnits; }
		void RunFixture(const FunctionRecord* fixture);
		void RunCase(const TestRegistry::Test& test);
		void RunClass(const ObjectType* owner, const std::vector<const TestRegistry::Test*>& tests);
		llvm::Constant* Text(const std::string& text);

		ModuleContext& m_MCtx;
		const TestRegistry& m_Tests;
		const TestHarnessOptions& m_Options;
		llvm::IRBuilder<> m_B;
		llvm::Function* m_Main = nullptr;
		std::unordered_map<std::string, llvm::Constant*> m_Texts;
	};
} // namespace Scripting
