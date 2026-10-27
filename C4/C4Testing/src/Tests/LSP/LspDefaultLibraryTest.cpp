#include "LspDefaultLibraryTest.h"
#include "LspTestSupport.h"
#include "Server/CompilationSession.h"
#include <filesystem>
#include <fstream>
#include <iterator>

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

		// The library file `relative` open in the editor with `text` (BUG-410): the URI an editor sends for it.
		std::string OpenLibraryFile(C4LSP::Server::CompilationSession& session, const std::string& relative, const std::string& extra)
		{
			const fs::path file = fs::absolute(fs::path(session.GetLibraryFolder()) / relative).lexically_normal();
			std::ifstream in(file, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const std::string uri = C4LSP::Protocol::DocumentUri::ToUri(file.string());
			session.SetOpenDocument(uri, text + extra);
			session.Recompile();
			return uri;
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

	void LspDefaultLibraryTest::TestAnOpenLibraryFileIsTheLibrarysOwn()
	{
		C4LSP::Server::CompilationSession session = Lsp::MakeSession("UsesArray.cf", kUsesArray);
		const std::string uri = OpenLibraryFile(session, "Container/array.ch", "\n// edited in the editor\n");
		TEST_ASSERT_EQ(DiagnosticsOf(session), std::string()); // not a second unit: no "redefinition of type 'Array'"
		const uint32 id = session.FindSourceIdByUri(uri);
		TEST_ASSERT(id != 0 && session.GetSourceManager().GetInfo(id).library);
		TEST_ASSERT(Lsp::Contains(session.GetSourceManager().GetInfo(id).source->Data(), "// edited in the editor")); // the editor's text
		TEST_PASSED();
	}

	void LspDefaultLibraryTest::TestAnOpenLibraryFileTheProgramDoesNotReachIsTheLibrarys()
	{
		C4LSP::Server::CompilationSession session = Lsp::MakeSession("UsesArray.cf", kUsesArray);
		const std::string uri = OpenLibraryFile(session, "Text/string.ch", "");
		TEST_ASSERT_EQ(DiagnosticsOf(session), std::string()); // the owner's "redefinition of type 'String'"
		const uint32 id = session.FindSourceIdByUri(uri);
		TEST_ASSERT(id != 0 && session.GetSourceManager().GetInfo(id).library);
		TEST_PASSED();
	}
} // namespace Testing
