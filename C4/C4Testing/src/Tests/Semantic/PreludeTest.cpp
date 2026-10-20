#include "PreludeTest.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include "Model/ProgramModel.h"
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		struct Program
		{
			CompilerInstance compiler;
			std::string diagnostics;

			const ProgramModel& Model() { return compiler.GetProgramModel(); }

			const SymbolInfo* RootEntry(const char* name, uint32 index = 0)
			{
				const auto& entries = Model().Symbols()->GetRoot()->Entries(name);
				return index < entries.Count() ? entries[index].get() : nullptr;
			}

			uint32 RootEntryCount(const char* name) { return Model().Symbols()->GetRoot()->Entries(name).Count(); }
		};

		// Compiles the sources (name, text pairs) as C4.exe does, with the default library when `withLibrary`.
		void Compile(Program& program, std::initializer_list<std::pair<const char*, const char*>> sources, bool withLibrary = true)
		{
			for (const auto& [name, text] : sources) program.compiler.RegisterInMemory(name, text);
			if (withLibrary) program.compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			program.compiler.CompileAll();
			program.compiler.AnalyzeProgram();
			for (const auto& e : program.compiler.GetAllDiagnostics()) program.diagnostics += std::string(e.message.c_str()) + "\n";
		}
	}

	void PreludeTest::PreludeFunctionsAreLinkedBuiltins()
	{
		Program program;
		Compile(program, { { "t.cf", "int32 main() { return (int32)strlen(\"ab\"); }" } });
		TEST_ASSERT_EQ(program.diagnostics, std::string());
		TEST_ASSERT(program.Model().PreludeUnit() != 0);
		const SymbolInfo* strlenSymbol = program.RootEntry("strlen");
		TEST_ASSERT_NOT_NULL(strlenSymbol);
		TEST_ASSERT_EQ(strlenSymbol->definingUnit, program.Model().PreludeUnit());
		const FunctionRecord* record = strlenSymbol->functions.Empty() ? nullptr : strlenSymbol->functions[0];
		TEST_ASSERT_NOT_NULL(record);
		TEST_ASSERT(record->isBuiltin && record->isLinked);
		TEST_ASSERT_EQ(std::string(record->mangledName.c_str()), std::string("strlen"));
		const SymbolInfo* allocate = program.RootEntry("raw_alloc");
		TEST_ASSERT(allocate && !allocate->functions.Empty());
		TEST_ASSERT_EQ(std::string(allocate->functions[0]->mangledName.c_str()), std::string("c4_raw_alloc"));
		TEST_PASSED();
	}

	void PreludeTest::EveryUnitSeesThePrelude()
	{
		Program program;
		Compile(program, {
			{ "a.cf", "int32 First() { printf(\"%d\\n\", 1); return 1; }" },
			{ "b.cf", "int32 Second(_GUID* g) { memset(g, 0, sizeof(_GUID)); return 2; }" },
		});
		TEST_ASSERT_EQ(program.diagnostics, std::string());
		TEST_PASSED();
	}

	void PreludeTest::ProgramFunctionHidesThePreludeOne()
	{
		Program program;
		Compile(program, { { "t.cf", "uint64 strlen(void* s) { return 7; }\nint32 main() { return (int32)strlen(null); }" } });
		TEST_ASSERT_EQ(program.diagnostics, std::string());
		TEST_ASSERT_EQ(program.RootEntryCount("strlen"), (uint32)1);
		const SymbolInfo* strlenSymbol = program.RootEntry("strlen");
		TEST_ASSERT(strlenSymbol->definingUnit != program.Model().PreludeUnit());
		TEST_ASSERT(!strlenSymbol->functions.Empty() && !strlenSymbol->functions[0]->isBuiltin);
		TEST_PASSED();
	}

	void PreludeTest::ProgramTypeHidesThePreludeOne()
	{
		Program program;
		Compile(program, { { "t.cf", "struct _GUID { int32 Data1; uint16 Data2; uint16 Data3; char[8] Data4; }\nint32 main() { return sizeof(_GUID) == 16 ? 0 : 1; }" } });
		TEST_ASSERT_EQ(program.diagnostics, std::string());
		TEST_ASSERT_EQ(program.RootEntryCount("_GUID"), (uint32)1);
		TEST_ASSERT(program.RootEntry("_GUID")->definingUnit != program.Model().PreludeUnit());
		TEST_PASSED();
	}

	void PreludeTest::NamespacedNamesDoNotHide()
	{
		Program program;
		Compile(program, { { "t.cf", "namespace Text { int32 strlen(int32 v) { return v; } }\nint32 main() { return (int32)strlen(\"x\") + Text::strlen(1); }" } });
		TEST_ASSERT_EQ(program.diagnostics, std::string());
		TEST_ASSERT_EQ(program.RootEntry("strlen")->definingUnit, program.Model().PreludeUnit());
		TEST_PASSED();
	}

	void PreludeTest::NoDefaultLibraryMeansNoPrelude()
	{
		Program program;
		Compile(program, { { "t.cf", "int32 main() { return 0; }" } }, false);
		TEST_ASSERT_EQ(program.Model().PreludeUnit(), (uint32)0);
		TEST_ASSERT_EQ(program.RootEntryCount("strlen"), (uint32)0);
		TEST_PASSED();
	}
}
