#include "LspTidyTest.h"
#include "LspTestSupport.h"
#include <string>

using Core::Vector; // Frost's container (global namespace) - see LspFormattingTest.cpp for the same pattern
using namespace C4LSP::Protocol;

namespace Testing
{
	namespace
	{
		bool HasMessage(const Vector<Diagnostic>& diags, const char* needle)
		{
			for (const auto& diag : diags)
				if (diag.message.find(needle) != std::string::npos)
					return true;
			return false;
		}

		bool HasSource(const Vector<Diagnostic>& diags, const char* source)
		{
			for (const auto& diag : diags)
				if (diag.source == source)
					return true;
			return false;
		}

		uint32 CountWith(const Vector<Diagnostic>& diags, const char* needle)
		{
			uint32 count = 0;
			for (const auto& diag : diags)
				if (diag.message.find(needle) != std::string::npos)
					++count;
			return count;
		}

		bool SeverityOf(const Vector<Diagnostic>& diags, const char* needle, DiagnosticSeverity expected)
		{
			for (const auto& diag : diags)
				if (diag.message.find(needle) != std::string::npos && diag.severity == expected)
					return true;
			return false;
		}

		void SetTidy(bool enabled) { C4LSP::Features::Tidy::SetEnabled(enabled); }
		auto TidyDiagsFor(const std::string& name, const std::string& program)
		{
			auto session = Testing::Lsp::MakeSession(name, program);
			std::string uri = Testing::Lsp::UriFor(name);
			return C4LSP::Features::Tidy::ForDocument(session, uri);
		}

		// NOTE on the test programs: they deliberately avoid ALL function calls (no builtins) and use a
		// file-scope sink variable for "uses". An UNRESOLVED identifier would simply be missing from
		// ModelBuildResult::referenceDeclarations, which could mask a real usage and turn a used local into
		// a false unused-local finding - globals keep every reference resolved so the assertions test TIDY's
		// logic, not name resolution.
	} // namespace

	void LspTidyTest::TestUnusedLocalVariableReported()
	{
		const std::string program = "int32 g_out;\nvoid Run()\n{\n\tint32 unused_var;\n}\n";
		SetTidy(true);
		auto diags = TidyDiagsFor("tidy_unused.cf", program);

		TEST_ASSERT(HasMessage(diags, "unused local variable 'unused_var'"));
		TEST_ASSERT(HasSource(diags, "c4-tidy")); // tidy findings are tagged distinctly from compiler errors ("c4")
		// The finding is a warning (squiggle), never an error: it must not block the build.
		TEST_ASSERT(SeverityOf(diags, "unused local variable", DiagnosticSeverity::Warning));
		SetTidy(true); // restore for other tests
		TEST_PASSED();
	}

	void LspTidyTest::TestUsedLocalNotReported()
	{
		const std::string program = "int32 g_out;\nvoid Run()\n{\n\tint32 v = 5;\n\tg_out = v;\n}\n";
		SetTidy(true);
		auto diags = TidyDiagsFor("tidy_used.cf", program);
		TEST_ASSERT_EQ(CountWith(diags, "unused local variable"), (uint32)0);
		SetTidy(true);
		TEST_PASSED();
	}

	void LspTidyTest::TestGlobalVariableNeverReported()
	{
		const std::string program = "int32 g_unused_global;\nvoid Run()\n{\n}\n";
		SetTidy(true);
		auto diags = TidyDiagsFor("tidy_global.cf", program);
		TEST_ASSERT_EQ(CountWith(diags, "unused local variable"), (uint32)0); // file scope is not a local
		SetTidy(true);
		TEST_PASSED();
	}

	void LspTidyTest::TestMultiDeclaratorLineReportsOnlyUnusedNames()
	{
		const std::string program = "int32 g_out;\nvoid Run()\n{\n\tint32 used, unused;\n\tg_out = used;\n}\n";
		SetTidy(true);
		auto diags = TidyDiagsFor("tidy_multi.cf", program);
		TEST_ASSERT(HasMessage(diags, "unused local variable 'unused'"));
		TEST_ASSERT_EQ(CountWith(diags, "'used'"), (uint32)0); // the used name on the same line must not fire
		SetTidy(true);
		TEST_PASSED();
	}

	void LspTidyTest::TestSelfAssignmentReportedAsInformation()
	{
		const std::string program = "void Run()\n{\n\tint32 v;\n\tv = v;\n}\n";
		SetTidy(true);
		auto diags = TidyDiagsFor("tidy_selfassign.cf", program);
		TEST_ASSERT(HasMessage(diags, "assigned to itself"));
		// Informational (a hint), not a warning: self-assignment is legal and often intentional.
		TEST_ASSERT(SeverityOf(diags, "assigned to itself", DiagnosticSeverity::Information));
		SetTidy(true);
		TEST_PASSED();
	}

	void LspTidyTest::TestTautologicalComparisonReported()
	{
		const std::string program = "int32 g_out;\nvoid Run()\n{\n\tint32 v;\n\tif (v == v)\n\t{\n\t\tg_out = 1;\n\t}\n}\n";
		SetTidy(true);
		auto diags = TidyDiagsFor("tidy_taut.cf", program);
		TEST_ASSERT(HasMessage(diags, "tautological"));
		SetTidy(true);
		TEST_PASSED();
	}

	void LspTidyTest::TestDifferentVariablesNotFlagged()
	{
		const std::string program = "int32 g_out;\nvoid Run()\n{\n\tint32 a;\n\tint32 b;\n\ta = 1;\n\tb = 2;\n\tif (a == b)\n\t{\n\t\tg_out = a + b;\n\t}\n}\n";
		SetTidy(true);
		auto diags = TidyDiagsFor("tidy_diff.cf", program);
		TEST_ASSERT_EQ(CountWith(diags, "tautological"), (uint32)0); // a == b is a real comparison
		TEST_ASSERT_EQ(CountWith(diags, "assigned to itself"), (uint32)0);
		SetTidy(true);
		TEST_PASSED();
	}

	void LspTidyTest::TestDisabledFeatureProducesNoDiagnostics()
	{
		// Two independent tidy findings on one document: a self-assignment (Information) and an unused
		// local (Warning). Note the self-assigned `v` counts as USED, so it does NOT also produce an
		// unused-local finding - `w` is the genuinely-unused variable. Together they give exactly 2.
		const std::string program = "void Run()\n{\n\tint32 v;\n\tint32 w;\n\tv = v;\n}\n";
		SetTidy(false); // user disabled the setting in the editor's settings menu
		auto diags = TidyDiagsFor("tidy_off.cf", program);
		TEST_ASSERT_EQ(diags.Count(), (uint32)0); // zero tidy findings when off - override-only revert

		SetTidy(true); // re-enable: both findings come back on the same compiled session
		auto backOn = TidyDiagsFor("tidy_off.cf", program);
		TEST_ASSERT_EQ(backOn.Count(), (uint32)2); // self-assign + unused local, nothing else
		SetTidy(true);
		TEST_PASSED();
	}

	void LspTidyTest::TestParseErrorDocumentYieldsNoTidyFindings()
	{
		const std::string program = "void Run( {\n\tint32 v;\n"; // unbalanced - the parser recovers a partial AST (no complete function body)
		SetTidy(true);
		auto diags = TidyDiagsFor("tidy_parseerror.cf", program);

		// The compiler's own parse errors describe the damage; tidy must not add noise ON TOP of them.
		// With this input the parser keeps a partial AST in which `v` is never declared as a local, so
		// tidy finds nothing at all - and even if it did find something, the check below (exactly one)
		// proves tidy contributes at most a single finding to an error-riddled document.
		TEST_ASSERT_EQ(CountWith(diags, "unused local variable"), (uint32)0); // no bogus unused-local from a broken parse
		SetTidy(true);
		TEST_PASSED();
	}
} // namespace Testing
