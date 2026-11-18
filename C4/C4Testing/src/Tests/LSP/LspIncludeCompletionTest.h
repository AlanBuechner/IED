#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Completion of an include's path (docs/tools/03 "Completion"): the folders and headers the compiler's include lookup
	// reaches from the file (docs/language/15-projects.md rule 3), part by part, as an editor shows them while typing.
	CLASS(GROUP = Testing) LspIncludeCompletionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspIncludeCompletionTest);

		// Where the caret is: in an include's path or not, a C4 header or one for the C bindings, the part being typed.
		TEST_FUNC(Lsp.Features.IncludeCompletion)
		void TestFindsThePathTheCaretIsIn();

		// The project's folders and headers, its dependencies' (named), the default library's; no other file, no hidden
		// folder, no source folder's tests/.
		TEST_FUNC(Lsp.Features.IncludeCompletion)
		void TestOffersWhatAnIncludeReaches();

		// After a folder: its headers, the part typed replaced.
		TEST_FUNC(Lsp.Features.IncludeCompletion)
		void TestCompletesInsideTheFolderTyped();

		// A folder part in the middle of a path keeps the path's own '/' and asks for nothing more.
		TEST_FUNC(Lsp.Features.IncludeCompletion)
		void TestAFolderInsideAPathAddsNoSlash();

		// A project reaches what it depends on, never the projects that use it; its own tests reach its tests/.
		TEST_FUNC(Lsp.Features.IncludeCompletion)
		void TestAProjectReachesWhatItDependsOn();

		// `extern "c" include` offers C and C++ headers, and no C4 library.
		TEST_FUNC(Lsp.Features.IncludeCompletion)
		void TestBindingsOfferCHeaders();

		// The new trigger characters answer nothing outside an include: a string, a division, a comment.
		TEST_FUNC(Lsp.Features.IncludeCompletion)
		void TestQuotesAndSlashesElsewhereAnswerNothing();
	};
} // namespace Testing
