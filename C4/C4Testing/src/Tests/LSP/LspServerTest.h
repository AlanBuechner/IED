#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Server/LspServer.h"
#include "Json/JsonValue.h"
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	// Drives the real LspServer over its public message API, exercising the full pipeline:
	// JSON-RPC framing -> dispatch -> feature -> framed output. Validates the initialize handshake,
	// capability advertisement, diagnostics publishing, hover end-to-end, error responses and the
	// shutdown/exit lifecycle.
	CLASS(GROUP = Testing) LspServerTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspServerTest);

		TEST_FUNC(Lsp.Server)
		void TestInitializeAdvertisesCapabilities();

		TEST_FUNC(Lsp.Server)
		void TestHoverEndToEndReturnsType();

		TEST_FUNC(Lsp.Server)
		void TestSemanticTokensCapabilityAndFullRequest();

		TEST_FUNC(Lsp.Server)
		void TestSemanticTokensWaitForPendingWorkWithoutExplicitSync();

		TEST_FUNC(Lsp.Server)
		void TestCompletionAfterDidChangeWaitsForRecompileWithoutExplicitSync();

		TEST_FUNC(Lsp.Server)
		void TestDidOpenPublishesEmptyDiagnosticsForValidProgram();

		TEST_FUNC(Lsp.Server)
		void TestDidOpenPublishesErrorsForInvalidProgram();

		TEST_FUNC(Lsp.Server)
		void TestSemanticDiagnosticsCarryNonZeroWidthRanges();

		TEST_FUNC(Lsp.Server)
		void TestMethodArgTypeErrorTargetsOffendingArgument();

		TEST_FUNC(Lsp.Server)
		void TestUnknownMethodReturnsMethodNotFound();

		TEST_FUNC(Lsp.Server)
		void TestFeatureBeforeInitializeIsRejected();

		TEST_FUNC(Lsp.Server)
		void TestShutdownThenExitTerminatesCleanly();

		TEST_FUNC(Lsp.Server)
		void TestInitializationOptionsIncludePathsResolveIncludes();

		TEST_FUNC(Lsp.Server)
		void TestInitializationOptionsDefinesSeedMacroTable();

		TEST_FUNC(Lsp.Server)
		void TestInitializationOptionsC4HomeIsTheDefaultLibrary();

		TEST_FUNC(Lsp.Server)
		void TestInitializationOptionsProjectsDescribeTheBuild();

		TEST_FUNC(Lsp.Server)
		void TestMalformedInitializationOptionsNeverBreakServer();

		TEST_FUNC(Lsp.Server)
		void TestFormattingCapabilityAndRequest();

		TEST_FUNC(Lsp.Server)
		void TestTidyFindingsPublishWithCompilerDiagnosticsWhenEnabled();

		TEST_FUNC(Lsp.Server)
		void TestDidChangeConfigurationTogglesTidyLive();

		// --- Code actions (the VS2022 lightbulb) ---
		// Quickfix for the compiler's dot-on-pointer error: one action that replaces exactly the '.'
		// token with '->', scoped to its squiggle, even when other errors exist in the file.
		TEST_FUNC(Lsp.Server)
		void TestCodeActionQuickfixReplacesDotOnPointer();

		// Source action for "Undefined identifier" on a free-function call: inserts a compilable stub
		// before the enclosing top-level definition (C4 requires declarations before use). Variable
		// uses never get one.
		TEST_FUNC(Lsp.Server)
		void TestCodeActionCreateDefinitionInsertsCompilableStub();

		// Both feature families are independently toggleable live through workspace/didChangeConfiguration
		// (what the client pushes from plugin settings): with both off the response is an empty array and the
		// server behaves as if the capability never existed.
		TEST_FUNC(Lsp.Server)
		void TestCodeActionsToggleOffViaDidChangeConfiguration();

		// textDocument/foldingRange items must use the LSP-spec field names `start`/`end`. Monaco's
		// folding model reads only those keys; non-standard names (e.g. startLine/endLine) parse as a
		// valid array but produce ZERO folds in the editor with no error anywhere - exactly the bug
		// this test locks out end-to-end over the real server pipeline.
		TEST_FUNC(Lsp.Server)
		void TestFoldingRangeResponseUsesLspFieldNames();

		// Semantic tokens must stay correctly classified when the file contains preprocessor
		// directives: MacroPass blanks #define lines and normalizes CRLF before parsing, so the AST's
		// effective-text coordinates differ from the editor text. Classification is keyed on (line, col),
		// which survives that transformation; this test locks it end-to-end over a REAL disk root (the
		// in-memory sessions used by other tests never run MacroPass and cannot catch coordinate drift).
		TEST_FUNC(Lsp.Server)
		void TestSemanticTokensStableAcrossPreprocessorDirectives();

private:
		std::vector<std::string> m_Framed;                 // captured wire output (header+body)
		std::vector<Json::Value> m_Messages;        // parsed messages (kept so returned pointers stay valid)
		std::unique_ptr<C4LSP::Server::LspServer> m_Server;

		void Reset(const std::string& rootUri);
		void FeedRequest(const std::string& method, int64 id, const Json::Value& params);
		void FeedNotification(const std::string& method, const Json::Value& params);
		void Refresh();
		const Json::Value* ResponseForId(int64 id);
		const Json::Value* NotificationByMethod(const std::string& method);
	// Cursor variant: returns the notification at position *cursor and advances it, so callers can
	// walk every publishDiagnostics frame (a disk-root recompile posts one per document). nullptr ends.
	const Json::Value* NotificationByMethod(const std::string& method, size_t* cursor);

		static Json::Value MakeTextDocParams(const std::string& uri, const std::string& text);
	};
} // namespace Testing
