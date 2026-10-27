#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// BUG-198: a session compiles the program with the default library the way C4.exe does - the Bedrock files the
	// program includes, from the install (Toolchain::Bedrock, or the folder the client names).
	CLASS(GROUP = Testing) LspDefaultLibraryTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspDefaultLibraryTest);

		TEST_FUNC(Lsp.Session)
		void TestBedrockIncludeResolvesWithoutDiagnostics();

		TEST_FUNC(Lsp.Session)
		void TestOnlyIncludedLibraryFilesAreCompiled();

		TEST_FUNC(Lsp.Session)
		void TestLibraryFolderWithoutBedrockLeavesTheIncludeUnresolved();

		TEST_FUNC(Lsp.Session)
		void TestAnOpenLibraryFileIsTheLibrarysOwn();

		TEST_FUNC(Lsp.Session)
		void TestAnOpenLibraryFileTheProgramDoesNotReachIsTheLibrarys();
	};
} // namespace Testing
