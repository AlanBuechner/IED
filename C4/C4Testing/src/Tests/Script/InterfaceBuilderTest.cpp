#include "InterfaceBuilderTest.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/Toolchain.h"
#include "Script/InterfaceBuilder.h"
#include <algorithm>
#include <optional>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		const char* kProgram =
			"include \"bedrock.ch\"\n"
			"int32 Score(int32 hits) {\n\treturn hits * 10;\n}\n"
			"int32 main() { return Score(1) - 10; }\n";

		// The interface of `source` compiled with the default library, or nothing.
		std::optional<HostInterface> InterfaceOf(const char* source, const std::vector<std::string>& symbols = { "Score_1int32" })
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("game.cf", source);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			HostBuild build;
			build.symbols = symbols;
			std::string error;
			return BuildInterface(compiler, {}, build, &error);
		}

		const InterfaceFile* FileNamed(const HostInterface& host, const std::string& path)
		{
			for (const InterfaceFile& file : host.files)
				if (file.path == path) return &file;
			return nullptr;
		}
	}

	void InterfaceBuilderTest::EverySourceIsAFileInUnitOrder()
	{
		const std::optional<HostInterface> host = InterfaceOf(kProgram);
		TEST_ASSERT(host.has_value());
		TEST_ASSERT(host->files.size() > 2);
		for (size_t i = 0; i < host->files.size(); ++i) TEST_ASSERT_EQ(host->files[i].unitId, (uint32)(i + 1));
		TEST_ASSERT(host->files[0].path == "game.cf" && !host->files[0].library && host->files[0].folder == -1);
		TEST_ASSERT(host->symbols == std::vector<std::string>{ "Score_1int32" });
		TEST_ASSERT_EQ(host->projects.size(), (size_t)1);
		TEST_PASSED();
	}

	void InterfaceBuilderTest::LibraryFilesAndThePreludeAreMarked()
	{
		const std::optional<HostInterface> host = InterfaceOf(kProgram);
		TEST_ASSERT(host.has_value());
		const InterfaceFile* prelude = FileNamed(*host, "Prelude/prelude.ch");
		TEST_ASSERT(prelude != nullptr && prelude->library && prelude->prelude && prelude->folder == -1);
		const InterfaceFile* umbrella = FileNamed(*host, "bedrock.ch");
		TEST_ASSERT(umbrella != nullptr && umbrella->library && !umbrella->prelude);
		TEST_ASSERT_EQ((int)std::count_if(host->files.begin(), host->files.end(), [](const InterfaceFile& f) { return f.prelude; }), 1);
		TEST_PASSED();
	}

	void InterfaceBuilderTest::TextsAreInterfaceTexts()
	{
		const std::optional<HostInterface> host = InterfaceOf(kProgram);
		TEST_ASSERT(host.has_value());
		const std::string& text = host->files[0].text;
		TEST_ASSERT(text.find("int32 Score(int32 hits) ;") != std::string::npos);
		TEST_ASSERT(text.find("hits * 10") == std::string::npos);
		TEST_ASSERT(text.find("include \"bedrock.ch\"") != std::string::npos);
		TEST_PASSED();
	}

	void InterfaceBuilderTest::TheBuildIdFollowsTheContent()
	{
		const std::optional<HostInterface> first = InterfaceOf(kProgram);
		const std::optional<HostInterface> again = InterfaceOf(kProgram);
		const std::optional<HostInterface> otherSymbols = InterfaceOf(kProgram, { "Score_1int32", "main_0" });
		const std::optional<HostInterface> otherText = InterfaceOf(
			"include \"bedrock.ch\"\nint32 Score(int32 hits, int32 bonus) { return hits; }\nint32 main() { return Score(0, 0); }\n");
		TEST_ASSERT(first && again && otherSymbols && otherText);
		TEST_ASSERT((first->buildId != std::array<uint8, 16>{}));
		TEST_ASSERT(first->buildId == again->buildId);
		TEST_ASSERT(first->buildId != otherSymbols->buildId);
		TEST_ASSERT(first->buildId != otherText->buildId);
		TEST_PASSED();
	}

	void InterfaceBuilderTest::PreludeFunctionsAreListed()
	{
		CompilerInstance compiler;
		compiler.RegisterInMemory("game.cf", kProgram);
		compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
		compiler.CompileAll();
		compiler.AnalyzeProgram();
		const std::vector<std::string> functions = PreludeFunctionSymbols(compiler.GetProgramModel());
		TEST_ASSERT(std::find(functions.begin(), functions.end(), "printf") != functions.end());
		TEST_ASSERT(std::find(functions.begin(), functions.end(), "c4_gc_collect") != functions.end());
		TEST_ASSERT(std::find(functions.begin(), functions.end(), "Score_1int32") == functions.end());
		TEST_PASSED();
	}
} // namespace Testing
