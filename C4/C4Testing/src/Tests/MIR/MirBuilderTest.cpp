#include "MirBuilderTest.h"
#include "Backend/HIR/LoweringDriver.h"
#include "Backend/MIR/MirText.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ErrorTracker.h"
#include "Model/ProgramModel.h"
#include <sstream>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		constexpr const char* c_Res =
			"struct Res { public int32 id; [operator(\"copy\")] public void Copy(const Res& o) { id = o.id; } "
			"[operator(\"delete\")] public void Delete() {} }\n"
			"int32 Peek(const Res& r) { return r.id; }\n"
			"int32 Sink(Res r) { return r.id; }\n"
			"int32 Next() { return 1; }\n";

		// F's MIR as text (empty when F did not lower completely), and whether it has MIR at all.
		struct Built
		{
			std::string text;
			bool complete = false;
			bool hasMir = false;
		};

		Built Build(const std::string& body)
		{
			CompilerInstance compiler;
			const std::string source = std::string(c_Res) + body;
			compiler.RegisterInMemory("t.cf", source.c_str());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			ProgramModel& model = compiler.GetProgramModelForLowering();
			const FunctionRecord* rec = nullptr;
			Built built;
			if (!model.QueryFunction("F", nullptr, nullptr, {}, {}, &rec).Ok() || !rec) return built;
			ErrorTracker errors;
			HIRFunction fn = LowerFunction(model, errors, rec);
			built.complete = fn.complete;
			built.hasMir = fn.mir != nullptr;
			if (fn.mir) built.text = MirText(*fn.mir);
			return built;
		}

		std::vector<std::string> Lines(const std::string& text)
		{
			std::vector<std::string> lines;
			std::istringstream in(text);
			for (std::string line; std::getline(in, line); ) lines.push_back(line);
			return lines;
		}

		int Count(const std::string& text, const std::string& what)
		{
			int count = 0;
			for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++count;
			return count;
		}

		bool Has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

		// The blocks that copy a cleanup on a normal way out (not in an exception's funclet).
		int NormalCleanupBlocks(const std::string& text)
		{
			int count = 0;
			for (const std::string& line : Lines(text))
				if (line.rfind("bb", 0) == 0 && Has(line, "cleanup") && !Has(line, "funclet")) ++count;
			return count;
		}
	}

	void MirBuilderTest::AJumpRunsTheCleanupsItLeaves()
	{
		// The break and the end of the body both leave `a`'s scope: each runs a copy of its cleanup.
		const Built built = Build("void F(bool c) { while (c) { Res a; if (c) break; Peek(a); } }\n");
		TEST_ASSERT(built.complete);
		TEST_ASSERT(NormalCleanupBlocks(built.text) >= 2);
		TEST_ASSERT(Has(built.text, "cleanup bb")); // and an exception's copy (an unwind step)
	}

	void MirBuilderTest::ReturnsShareTheirWayOut()
	{
		// Two returns cross `a`'s cleanup: both stage their value and take the one way out, built once.
		const Built built = Build("int32 F(bool c) { Res a; if (c) return 1; Peek(a); if (!c) return 2; return 3; }\n");
		TEST_ASSERT(built.complete);
		TEST_ASSERT(Count(built.text, "stage return value") == 3);
		TEST_ASSERT(Count(built.text, "return staged") == 1);
		// The way out's copy, and the end of the body's, which nothing reaches (every path returns).
		TEST_ASSERT(NormalCleanupBlocks(built.text) == 2);
		TEST_ASSERT(Has(built.text, "fall off"));
	}

	void MirBuilderTest::ACallUnwindsThroughTheCleanupsAroundIt()
	{
		const Built built = Build("void F() { Res a; Peek(a); }\n");
		TEST_ASSERT(built.complete);
		TEST_ASSERT(Has(built.text, "s0 cleanup bb"));
		TEST_ASSERT(Has(built.text, "then caller"));
		TEST_ASSERT(Has(built.text, "(unwind s0"));
		TEST_ASSERT(Has(built.text, "unwind to caller")); // the copy's end: the exception goes on
	}

	void MirBuilderTest::ATryDispatchesToItsHandlers()
	{
		const Built built = Build("void F() { try { Peek(Res()); } catch (const Res& r) { Peek(r); } catch (...) { } }\n");
		TEST_ASSERT(built.complete);
		TEST_ASSERT(Has(built.text, "dispatch h0 h1 then caller"));
		TEST_ASSERT(Has(built.text, "h0 bb") && Has(built.text, " catch\n"));
		TEST_ASSERT(Has(built.text, "h1 bb") && Has(built.text, "catch all"));
		TEST_ASSERT(Has(built.text, "leave h0 to") && Has(built.text, "leave h1 to"));
		TEST_ASSERT(Has(built.text, "handler h0") && Has(built.text, "handler h1"));
	}

	void MirBuilderTest::AnExpressionRunsItsBlocksAsRegions()
	{
		// Sink(a) copies `a` (it is used again), and the copy is built by a block the call's argument runs in place.
		const Built built = Build("void F() { Res a; Sink(a); Peek(a); }\n");
		TEST_ASSERT(built.complete);
		TEST_ASSERT(Has(built.text, "eval (1 region)"));
		TEST_ASSERT(Has(built.text, "in place"));
		TEST_ASSERT(Has(built.text, "region end"));
	}

	void MirBuilderTest::ANoexceptBodyRunsInACatchAll()
	{
		const Built built = Build("void F() noexcept { Peek(Res()); }\n");
		TEST_ASSERT(built.complete);
		TEST_ASSERT(Has(built.text, "dispatch h0 then caller"));
		TEST_ASSERT(Has(built.text, " terminate\n"));
		TEST_ASSERT(Has(built.text, "  terminate\n"));
	}

	void MirBuilderTest::AStaticLocalStartsOnce()
	{
		const Built built = Build("void F() { static int32 n = Next(); }\n");
		TEST_ASSERT(built.complete);
		TEST_ASSERT(Has(built.text, "once bb"));
		TEST_ASSERT(Has(built.text, "once end"));
		TEST_ASSERT(Has(built.text, "abandon")); // an exception leaving the start gives up its guard
	}

	void MirBuilderTest::AMovedVariablesCleanupTestsItsFlag()
	{
		// `a`'s last use moves it: its flag is set where it is declared, and each copy of its cleanup tests it.
		const Built built = Build("void F() { Res a; Res b = a; }\n");
		TEST_ASSERT(built.complete);
		TEST_ASSERT(Has(built.text, "flag %"));
		bool guardBranches = false;
		const std::vector<std::string> lines = Lines(built.text);
		for (size_t i = 0; i + 1 < lines.size(); ++i)
			if (lines[i].rfind("bb", 0) == 0 && Has(lines[i], "cleanup") && Has(lines[i + 1], "branch")) guardBranches = true;
		TEST_ASSERT(guardBranches);
		// A variable that is not moved has no flag.
		const Built kept = Build("void F() { Res a; Res b = a; Peek(a); }\n");
		TEST_ASSERT(kept.complete && !Has(kept.text, "flag %"));
	}

	void MirBuilderTest::AnIncompleteBodyHasNoMir()
	{
		const Built built = Build("int32 F(bool c) { if (c) return 1; }\n"); // D-55: control reaches the end
		TEST_ASSERT(!built.complete && !built.hasMir);
	}
} // namespace Testing
