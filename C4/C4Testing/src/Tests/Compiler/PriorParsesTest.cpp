#include "PriorParsesTest.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/PriorParses.h"
#include "Compiler/Toolchain.h"
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kMain = "include \"lib.ch\"\nint32 main() { return Twice(1) - 2; }\n";
		const char* kLib = "int32 Twice(int32 v) { return v * 2; }\n";
		const char* kExtra = "int32 Thrice(int32 v) { return v * 3; }\n";

		// The program's own files, in the order they are registered (their ids, from 1).
		using Files = std::vector<std::pair<std::string, std::string>>;

		Files Program(const std::string& main, const std::string& lib = kLib)
		{
			return { { "main.cf", main }, { "lib.ch", lib }, { "extra.ch", kExtra } };
		}

		// Compiles and analyzes `files` (then the default library), taking `prior`'s trees for the files whose text they parsed.
		std::unique_ptr<CompilerInstance> Compile(const Files& files, std::shared_ptr<const PriorParses> prior = nullptr)
		{
			auto compiler = std::make_unique<CompilerInstance>();
			compiler->UsePriorParses(std::move(prior));
			for (const auto& [name, text] : files) compiler->RegisterInMemory(name, text);
			compiler->RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler->CompileAll();
			compiler->AnalyzeProgram();
			return compiler;
		}

		// Every diagnostic, as `file:line:column message`, in any order.
		std::multiset<std::string> Diagnostics(const CompilerInstance& compiler)
		{
			std::multiset<std::string> said;
			for (const CompilationError& error : compiler.GetAllDiagnostics())
				said.insert(std::to_string(error.location.sourceId) + ":" + std::to_string(error.location.line) + ":" +
					std::to_string(error.location.column) + " " + error.message);
			return said;
		}

		bool Says(const CompilerInstance& compiler, const std::string& part)
		{
			for (const std::string& line : Diagnostics(compiler))
				if (line.find(part) != std::string::npos) return true;
			return false;
		}

		const NamespaceNode* TreeOf(CompilerInstance& compiler, uint32 id) { return compiler.GetCompilationUnit(id)->GetRootAST().Get(); }
	}

	void PriorParsesTest::AnUnchangedFileTakesItsTree()
	{
		std::unique_ptr<CompilerInstance> first = Compile(Program(kMain));
		const NamespaceNode* lib = TreeOf(*first, 2);
		const NamespaceNode* main = TreeOf(*first, 1);
		std::shared_ptr<const PriorParses> parses = first->Parses();
		TEST_ASSERT_EQ(parses->Count(), static_cast<size_t>(first->SourceCount()));
		first.reset(); // the trees outlive the compile that parsed them
		std::unique_ptr<CompilerInstance> second = Compile(Program("include \"lib.ch\"\nint32 main() { return Twice(2) - 4; }\n"), parses);
		TEST_ASSERT_EQ(second->ReusedParses(), second->SourceCount() - 1); // the default library's files too
		TEST_ASSERT(TreeOf(*second, 2) == lib);
		TEST_ASSERT(TreeOf(*second, 1) != main);
		TEST_ASSERT(second->GetAllDiagnostics().Empty());
		TEST_PASSED();
	}

	void PriorParsesTest::ACompileWithTheTreesSaysWhatAFreshOneSays()
	{
		const std::string broken = "include \"lib.ch\"\nint32 main() { return Twice(1) -; }\nint32 Other() { return Missing(); }\n";
		std::unique_ptr<CompilerInstance> first = Compile(Program(broken, "int32 Twice(int32 v) { return v * ; }\n"));
		std::unique_ptr<CompilerInstance> again = Compile(Program(broken, "int32 Twice(int32 v) { return v * ; }\n"), first->Parses());
		TEST_ASSERT_EQ(again->ReusedParses(), again->SourceCount());
		TEST_ASSERT(!Diagnostics(*first).empty());
		TEST_ASSERT(Diagnostics(*again) == Diagnostics(*first));
		TEST_PASSED();
	}

	void PriorParsesTest::ATakenTreeReachesWhatItsIncludesIncludeNow()
	{
		std::unique_ptr<CompilerInstance> first = Compile(Program(kMain));
		TEST_ASSERT(!first->GetCompilationUnit(1)->GetIncludes().count(3));
		// lib.ch now includes extra.ch: main.cf, taken as it was, reaches extra.ch through it.
		const std::string lib = std::string("include \"extra.ch\"\n") + kLib;
		std::unique_ptr<CompilerInstance> second = Compile(Program("include \"lib.ch\"\nint32 main() { return Twice(1) - Thrice(0) - 2; }\n", lib), first->Parses());
		std::unique_ptr<CompilerInstance> third = Compile(Program("include \"lib.ch\"\nint32 main() { return Twice(1) - Thrice(0) - 2; }\n", lib), second->Parses());
		TEST_ASSERT(TreeOf(*third, 1) == TreeOf(*second, 1));
		TEST_ASSERT(third->GetCompilationUnit(1)->GetIncludes().count(3));
		std::unique_ptr<CompilerInstance> back = Compile(Program("include \"lib.ch\"\nint32 main() { return Twice(1) - Thrice(0) - 2; }\n"), third->Parses());
		TEST_ASSERT(TreeOf(*back, 1) == TreeOf(*third, 1));
		TEST_ASSERT(!back->GetCompilationUnit(1)->GetIncludes().count(3)); // lib.ch includes it no more
		TEST_PASSED();
	}

	void PriorParsesTest::ATakenTreeReportsItsIncludesAsAParseWould()
	{
		// `gone.ch` is not there; `lib.c` is no header, and the answer that it is not found either is not reported again
		// at the place the parse reported that.
		const std::string main = "include \"gone.ch\"\ninclude \"lib.c\"\nint32 main() { return 0; }\n";
		std::unique_ptr<CompilerInstance> first = Compile(Program(main));
		TEST_ASSERT(Says(*first, "include not found: 'gone.ch'"));
		TEST_ASSERT(Says(*first, ".ch' extension"));
		std::unique_ptr<CompilerInstance> again = Compile(Program(main), first->Parses());
		TEST_ASSERT(TreeOf(*again, 1) == TreeOf(*first, 1));
		TEST_ASSERT(Diagnostics(*again) == Diagnostics(*first));
		std::unique_ptr<CompilerInstance> third = Compile(Program(main), again->Parses());
		TEST_ASSERT(Diagnostics(*third) == Diagnostics(*first));
		TEST_PASSED();
	}

	void PriorParsesTest::AFileNowThereAnswersATakenTreesInclude()
	{
		const std::string main = "include \"gone.ch\"\nint32 main() { return Gone(); }\n";
		std::unique_ptr<CompilerInstance> first = Compile(Program(main));
		TEST_ASSERT(Says(*first, "include not found: 'gone.ch'"));
		Files withIt = Program(main);
		withIt.push_back({ "gone.ch", "int32 Gone() { return 0; }\n" });
		std::unique_ptr<CompilerInstance> second = Compile(withIt, first->Parses());
		TEST_ASSERT(TreeOf(*second, 1) == TreeOf(*first, 1));
		TEST_ASSERT(second->GetAllDiagnostics().Empty());
		TEST_PASSED();
	}

	void PriorParsesTest::AnotherTextIsParsed()
	{
		std::unique_ptr<CompilerInstance> first = Compile(Program(kMain));
		// The same text under other ids (a file registered before them): each is parsed again, its locations its own id's.
		Files shifted = Program(kMain);
		shifted.insert(shifted.begin(), { "first.ch", "int32 First() { return 1; }\n" });
		std::unique_ptr<CompilerInstance> second = Compile(shifted, first->Parses());
		TEST_ASSERT(TreeOf(*second, 2) != TreeOf(*first, 1));
		TEST_ASSERT_EQ(TreeOf(*second, 2)->Children().back()->startLocation.sourceId, 2u); // `main`'s declaration
		TEST_ASSERT(second->GetAllDiagnostics().Empty());
		TEST_PASSED();
	}
} // namespace Testing
