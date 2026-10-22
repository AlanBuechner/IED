#include "IncludeMergeTest.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/IncludeDeclarationNode.h"

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// main.cf (unit 1) includes lib.ch, which defines Twice.
		void Analyze(CompilerInstance& compiler)
		{
			compiler.RegisterInMemory("main.cf", "include \"lib.ch\"\nint32 main() { return Twice(1) - 2; }\n");
			compiler.RegisterInMemory("lib.ch", "int32 Twice(int32 v) { return v * 2; }\n");
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
		}

		struct TopLevel
		{
			int includes = 0;
			int functions = 0;
		};

		TopLevel TopLevelOf(CompilerInstance& compiler, uint32 unit)
		{
			TopLevel found;
			for (const Ref<AstNode>& child : compiler.GetCompilationUnit(unit)->GetRootAST()->Children())
			{
				if (child.PointerCast<IncludeDeclarationNode>()) found.includes++;
				if (child.PointerCast<FunctionDefinitionNode>()) found.functions++;
			}
			return found;
		}
	}

	void IncludeMergeTest::AnalyzingLeavesEachTreeItsOwn()
	{
		CompilerInstance compiler;
		Analyze(compiler);
		TEST_ASSERT(compiler.GetAllDiagnostics().Empty());
		const TopLevel main = TopLevelOf(compiler, 1);
		TEST_ASSERT_EQ(main.includes, 1);
		TEST_ASSERT_EQ(main.functions, 1);
	}

	void IncludeMergeTest::MergingSplicesTheIncludedDeclarations()
	{
		CompilerInstance compiler;
		Analyze(compiler);
		compiler.MergeIncludes();
		const TopLevel main = TopLevelOf(compiler, 1);
		TEST_ASSERT_EQ(main.includes, 0);
		TEST_ASSERT_EQ(main.functions, 2);
	}
} // namespace Testing
