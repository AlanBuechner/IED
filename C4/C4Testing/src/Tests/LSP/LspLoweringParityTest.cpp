#include "LspLoweringParityTest.h"
#include "Server/CompilationSession.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Server/ProgramOptions.h"
#include <llvm/IR/LLVMContext.h>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		constexpr const char* kFile = "Parity.cf";

		// A class template whose `Less` needs `<` on T, and whose virtual `Bad` does too.
		constexpr const char* kTemplates =
			"template<T> class Box\n"
			"{\n"
			"\tpublic T value;\n"
			"\tpublic bool Less(const Box<T>& other) { return value < other.value; }\n"
			"\tpublic T Get() { return value; }\n"
			"}\n"
			"template<T> class Shape\n"
			"{\n"
			"\tpublic T value;\n"
			"\tpublic virtual bool Bad() { return value < value; }\n"
			"\tpublic int32 Size() { return 1; }\n"
			"}\n"
			"struct Plain { public int32 x; }\n";

		std::string Described(const CompilationError& error)
		{
			const char* severity = error.severity == ErrorSeverity::Warning ? "warning" : "error";
			return std::to_string(error.location.line) + ":" + std::to_string(error.location.column) + " " + severity + ": " + error.message.c_str();
		}

		// What C4.exe reports for the program (`--script-host` with `scriptHost`): the front end's diagnostics, then, after a
		// clean front end, the bodies'.
		std::vector<std::string> BuildDiagnostics(const std::string& program, bool scriptHost)
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory(kFile, program.c_str());
			compiler.CompileAll();
			const bool modelOk = compiler.AnalyzeProgram();
			std::vector<std::string> out;
			bool failed = !modelOk;
			for (const CompilationError& error : compiler.GetAllDiagnostics())
			{
				out.push_back(Described(error));
				failed = failed || error.severity != ErrorSeverity::Warning;
			}
			if (failed) return out;
			llvm::LLVMContext context;
			ProgramBackend driver(context);
			if (scriptHost) driver.ShareVtables();
			driver.Lower(compiler.GetProgramModelForLowering(), "main");
			for (const CompilationError& error : driver.LoweringErrors()) out.push_back(Described(error));
			return out;
		}

		// What the language server shows for the same program, told it builds with `--script-host` by `scriptHost`.
		std::vector<std::string> EditorDiagnostics(const std::string& program, bool scriptHost)
		{
			C4LSP::Server::CompilationSession session;
			session.SetScriptHostBuild(scriptHost);
			session.RegisterInMemorySource(kFile, program);
			session.Recompile();
			std::vector<std::string> out;
			for (const CompilationError& error : session.GetCompiler().GetAllDiagnostics()) out.push_back(Described(error));
			for (const CompilationError& error : session.GetLoweringDiagnostics()) out.push_back(Described(error));
			return out;
		}

		std::string Joined(const std::vector<std::string>& lines)
		{
			std::string text;
			for (const std::string& line : lines) text += line + "\n";
			return text;
		}

		// The build's diagnostics, which the editor's must equal.
		std::vector<std::string> SameInBoth(const std::string& program, bool scriptHost = false)
		{
			const std::vector<std::string> build = BuildDiagnostics(program, scriptHost);
			const std::vector<std::string> editor = EditorDiagnostics(program, scriptHost);
			if (build != editor) std::printf("build:\n%seditor:\n%s", Joined(build).c_str(), Joined(editor).c_str());
			TEST_ASSERT(build == editor);
			return build;
		}

		bool AnyMentions(const std::vector<std::string>& lines, const std::string& text)
		{
			for (const std::string& line : lines)
				if (line.find(text) != std::string::npos) return true;
			return false;
		}
	}

	void LspLoweringParityTest::AnUnusedMemberOfAnInstanceIsCheckedByNeither()
	{
		const std::vector<std::string> diagnostics = SameInBoth(std::string(kTemplates) +
			"int32 main() { Box<Plain> b; return b.Get().x; }\n");
		TEST_ASSERT(diagnostics.empty());
	}

	void LspLoweringParityTest::ACalledMemberIsReportedAlikeWithItsUse()
	{
		const std::vector<std::string> diagnostics = SameInBoth(std::string(kTemplates) +
			"int32 main() { Box<Plain> b; return b.Less(b) ? 1 : 0; }\n");
		TEST_ASSERT(!diagnostics.empty());
		TEST_ASSERT(AnyMentions(diagnostics, "Box<Plain>::Less', used at"));
	}

	void LspLoweringParityTest::AVirtualMemberOfAMadeInstanceIsReportedByBoth()
	{
		const std::vector<std::string> diagnostics = SameInBoth(std::string(kTemplates) +
			"int32 main() { Shape<Plain> s; return s.Size(); }\n");
		TEST_ASSERT(AnyMentions(diagnostics, "Shape<Plain>::Bad"));
	}

	// Reflection can make every instance it registers (docs/runtime/02 section 2), so the instance's vtable is the
	// program's whether or not code makes one.
	void LspLoweringParityTest::AVirtualMemberOfAnInstanceOnlyPointedAtIsReportedByBoth()
	{
		const std::vector<std::string> diagnostics = SameInBoth(std::string(kTemplates) +
			"int32 Count(Shape<Plain>* s) { return s == null ? 0 : s->Size(); }\n"
			"int32 main() { return Count(null); }\n");
		TEST_ASSERT(AnyMentions(diagnostics, "Shape<Plain>::Bad"));
	}

	void LspLoweringParityTest::AGlobalOfAnInstanceNeedsItsVirtualMembers()
	{
		const std::vector<std::string> diagnostics = SameInBoth(std::string(kTemplates) +
			"Shape<Plain> shape;\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT(AnyMentions(diagnostics, "Shape<Plain>::Bad"));
	}

	void LspLoweringParityTest::AGlobalStartReachesTheMembersItUses()
	{
		const std::vector<std::string> diagnostics = SameInBoth(std::string(kTemplates) +
			"Box<Plain> first;\n"
			"bool ordered = first.Less(first);\n"
			"int32 main() { return ordered ? 1 : 0; }\n");
		TEST_ASSERT(AnyMentions(diagnostics, "Box<Plain>::Less"));
	}

	// A script host's build gives every class its vtables; the server, told so by Nuke's options, lowers alike.
	void LspLoweringParityTest::AScriptHostIsLoweredAlikeInBoth()
	{
		const std::vector<std::string> diagnostics = SameInBoth(std::string(kTemplates) +
			"int32 Count(Shape<Plain>* s) { return s == null ? 0 : s->Size(); }\n"
			"int32 main() { return Count(null); }\n", true);
		TEST_ASSERT(AnyMentions(diagnostics, "Shape<Plain>::Bad"));
	}

	void LspLoweringParityTest::TheProgramOptionsSayWhetherItIsAScriptHost()
	{
		Json::Value options = Json::Value::MakeObject();
		TEST_ASSERT(!C4LSP::Server::ReadProgramOptions("game", options).scriptHost);
		options.SetMember("scriptHost", Json::Value::Bool(true));
		TEST_ASSERT(C4LSP::Server::ReadProgramOptions("game", options).scriptHost);
		options.SetMember("scriptHost", Json::Value::String("yes"));
		TEST_ASSERT(!C4LSP::Server::ReadProgramOptions("game", options).scriptHost); // a malformed option is left out
	}

	void LspLoweringParityTest::ACleanProgramIsCleanInBoth()
	{
		const std::vector<std::string> diagnostics = SameInBoth(std::string(kTemplates) +
			"template<T> T Larger(T a, T b) { return a > b ? a : b; }\n"
			"Box<int32> boxed;\n"
			"int32 main()\n"
			"{\n"
			"\tShape<int32> s;\n"
			"\tfuncptr<int32(int32)> twice = [](int32 v) -> int32 { return v * 2; };\n"
			"\treturn Larger(twice(boxed.Get()), s.Size()) + (boxed.Less(boxed) ? 1 : 0);\n"
			"}\n");
		TEST_ASSERT(diagnostics.empty());
	}
} // namespace Testing
