#include "BindingUnitsTest.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/PriorParses.h"
#include "Compiler/Toolchain.h"
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kVecBinding = "struct Vec2\n{\n\tpublic float x;\n\tpublic float y;\n}\nconst int32 VEC_LANES = 2;\n";
		const std::string kVecPath = "C:/c4-tests/interop/0001_vec.h.00ff.c4bind";
		const std::string kOtherPath = "C:/c4-tests/interop/0002_shapes.h.00ff.c4bind";

		struct Program
		{
			std::unique_ptr<CompilerInstance> compiler = std::make_unique<CompilerInstance>();
			uint32 main = 0;
			uint32 lib = 0;

			SourceManager& Sources() { return compiler->GetSourceManager(); }

			bool Analyze()
			{
				compiler->RegisterIncludedLibraryFiles(Toolchain::Bedrock());
				compiler->CompileAll();
				return compiler->AnalyzeProgram();
			}

			bool Says(const std::string& part) const
			{
				for (const CompilationError& error : compiler->GetAllDiagnostics())
					if (std::string(error.message.c_str()).find(part) != std::string::npos) return true;
				return false;
			}
		};

		// main.cf includes lib.ch, which binds vec.h.
		Program BindingProgram(const std::string& main, const std::string& lib)
		{
			Program program;
			program.main = program.compiler->RegisterInMemory("main.cf", main);
			program.lib = program.compiler->RegisterInMemory("lib.ch", lib);
			return program;
		}
	}

	void BindingUnitsTest::BindingIsRegisteredOnceForEveryFileThatBindsIt()
	{
		SourceManager sources;
		const uint32 a = sources.RegisterInMemory("a.cf", "extern \"c\" include \"vec.h\";\n");
		const uint32 b = sources.RegisterInMemory("b.cf", "extern \"c\" include \"vec.h\";\n");
		const uint32 first = sources.RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, a);
		const uint32 second = sources.RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, b);
		TEST_ASSERT_EQ(first, second);
		TEST_ASSERT(sources.GetInfo(first).binding);
		TEST_ASSERT(!sources.GetInfo(a).binding);
		TEST_ASSERT_EQ(sources.RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, a), first); // a file binding it twice
		TEST_ASSERT(sources.GetInfo(a).bindings == std::vector<uint32>({ first }));
		TEST_ASSERT(sources.GetInfo(b).bindings == std::vector<uint32>({ first }));
		TEST_ASSERT_EQ(sources.GetInfo(first).project, sources.GetInfo(a).project);
		TEST_PASSED();
	}

	void BindingUnitsTest::BindingIsIncludedByItsAbsolutePathOnly()
	{
		SourceManager sources;
		const uint32 a = sources.RegisterInMemory("a.cf", "int32 x;\n");
		const uint32 binding = sources.RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, a);
		TEST_ASSERT_EQ(sources.ResolveInclude(kVecPath, a).id, binding);
		TEST_ASSERT_EQ(sources.ResolveInclude("C:/c4-tests/interop/missing.c4bind", a).id, uint32(0));
		TEST_PASSED();
	}

	void BindingUnitsTest::IncluderAndItsIncludersSeeTheBindings()
	{
		Program program = BindingProgram("include \"lib.ch\"\nVec2 g_Point;\nint32 Lanes() { return VEC_LANES; }\n",
			"extern \"c\" include \"vec.h\";\nVec2 Origin();\n");
		const uint32 binding = program.Sources().RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, program.lib);
		TEST_ASSERT(program.Analyze());
		TEST_ASSERT(program.compiler->GetCompilationUnit(program.lib)->GetIncludes().count(binding) == 1);
		TEST_ASSERT(program.compiler->GetCompilationUnit(program.main)->GetIncludes().count(binding) == 1);
		TEST_PASSED();
	}

	void BindingUnitsTest::TwoBindingsOfOneDeclarationAreOne()
	{
		// Two headers whose bindings both declare Vec2 (both include a third, say): one type, as when they were appended.
		Program program = BindingProgram("include \"lib.ch\"\nVec2 g_Point;\n",
			"extern \"c\" include \"vec.h\";\nextern \"c\" include \"shapes.h\";\n");
		program.Sources().RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, program.lib);
		program.Sources().RegisterBinding(kOtherPath, "shapes.h.c4bind", std::string(kVecBinding) + "struct Box { public Vec2 min; }\n", program.lib);
		TEST_ASSERT(program.Analyze());
		TEST_ASSERT(!program.Says("redefinition"));
		TEST_PASSED();
	}

	void BindingUnitsTest::IncludersLinesAreItsOwn()
	{
		Program program = BindingProgram("include \"lib.ch\"\n", "extern \"c\" include \"vec.h\";\n\nUnknownType g_Broken;\n");
		const uint32 binding = program.Sources().RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, program.lib);
		TEST_ASSERT(!program.Analyze());
		bool atItsLine = false;
		for (const CompilationError& error : program.compiler->GetAllDiagnostics())
			atItsLine = atItsLine || (error.location.sourceId == program.lib && error.location.line == 3);
		TEST_ASSERT(atItsLine);
		const SymbolInfo* vec = nullptr;
		program.compiler->GetProgramModel().Symbols()->ForEachScopedSymbol([&](const Core::String& name, SymbolInfo* info, const Scope&)
		{
			if (name == "Vec2") vec = info;
		});
		TEST_ASSERT(vec != nullptr && vec->definingUnit == binding); // declared in the binding unit, at its own line
		TEST_PASSED();
	}

	void BindingUnitsTest::PriorParseIsTakenOnlyForTheSameBindings()
	{
		Program first = BindingProgram("include \"lib.ch\"\nVec2 g_Point;\n", "extern \"c\" include \"vec.h\";\n");
		first.Sources().RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, first.lib);
		TEST_ASSERT(first.Analyze());
		std::shared_ptr<const PriorParses> parses = first.compiler->Parses();

		Program same = BindingProgram("include \"lib.ch\"\nVec2 g_Point;\n", "extern \"c\" include \"vec.h\";\n");
		same.compiler->UsePriorParses(parses);
		same.Sources().RegisterBinding(kVecPath, "vec.h.c4bind", kVecBinding, same.lib);
		TEST_ASSERT(same.Analyze());
		TEST_ASSERT(same.compiler->GetCompilationUnit(same.lib)->GetRootAST() == first.compiler->GetCompilationUnit(first.lib)->GetRootAST());

		Program other = BindingProgram("include \"lib.ch\"\nVec2 g_Point;\n", "extern \"c\" include \"vec.h\";\n");
		other.compiler->UsePriorParses(parses);
		other.Sources().RegisterBinding(kOtherPath, "shapes.h.c4bind", kVecBinding, other.lib); // the header binds another way now
		TEST_ASSERT(other.Analyze());
		TEST_ASSERT(other.compiler->GetCompilationUnit(other.lib)->GetRootAST() != first.compiler->GetCompilationUnit(first.lib)->GetRootAST());
		TEST_PASSED();
	}
}
