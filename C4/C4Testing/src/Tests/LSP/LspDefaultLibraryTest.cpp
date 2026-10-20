#include "LspDefaultLibraryTest.h"
#include "LspTestSupport.h"
#include "Server/CompilationSession.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace Testing
{
	namespace
	{
		const char* const kUsesArray =
			"include \"Container/array.ch\"\n"
			"int64 Count() { Core::Array<int32> a; a.Push(1); a.Push(2); return a.Count(); }\n";

		std::string DiagnosticsOf(const C4LSP::Server::CompilationSession& session)
		{
			std::string all;
			for (const auto& e : session.GetCompiler().GetAllDiagnostics()) all += std::string(e.message.c_str()) + "\n";
			for (const auto& e : session.GetLoweringDiagnostics()) all += std::string(e.message.c_str()) + "\n";
			return all;
		}

		bool Registered(const C4LSP::Server::CompilationSession& session, const std::string& displayName)
		{
			return session.GetSourceManager().FindSourceId(displayName) != 0;
		}
	}

	void LspDefaultLibraryTest::TestBedrockIncludeResolvesWithoutDiagnostics()
	{
		C4LSP::Server::CompilationSession session = Lsp::MakeSession("UsesArray.cf", kUsesArray);
		TEST_ASSERT_EQ(DiagnosticsOf(session), std::string());
		TEST_ASSERT(Registered(session, "Container/array.ch"));
		TEST_PASSED();
	}

	void LspDefaultLibraryTest::TestOnlyIncludedLibraryFilesAreCompiled()
	{
		C4LSP::Server::CompilationSession session = Lsp::MakeSession("UsesArray.cf", kUsesArray);
		TEST_ASSERT(Registered(session, "Container/array.cf")); // the header's implementation comes with it
		TEST_ASSERT(!Registered(session, "Container/hashmap.ch"));
		TEST_PASSED();
	}

	void LspDefaultLibraryTest::TestLibraryFolderWithoutBedrockLeavesTheIncludeUnresolved()
	{
		const fs::path empty = fs::temp_directory_path() / "c4lsp_no_bedrock";
		fs::create_directories(empty);
		C4LSP::Server::CompilationSession session;
		session.SetLibraryFolder(empty.string());
		session.RegisterInMemorySource("UsesArray.cf", kUsesArray);
		session.Recompile();
		TEST_ASSERT(Lsp::Contains(DiagnosticsOf(session), "include not found: 'Container/array.ch'"));
		fs::remove_all(empty);
		TEST_PASSED();
	}
} // namespace Testing
