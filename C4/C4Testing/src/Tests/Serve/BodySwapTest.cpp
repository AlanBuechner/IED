#include "BodySwapTest.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Serve/BodyEdits.h"
#include "Serve/BodySwap.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		using Files = std::vector<std::pair<std::string, std::string>>; // name, text

		std::unique_ptr<CompilerInstance> Parsed(const Files& files, std::shared_ptr<const PriorParses> prior)
		{
			auto compiler = std::make_unique<CompilerInstance>();
			for (const auto& [name, text] : files) compiler->RegisterInMemory(name, text);
			compiler->UsePriorParses(std::move(prior));
			compiler->CompileAll();
			return compiler;
		}

		// The refusal of the edit of `main.cf` from `before` to `after` (FindBodyEdit); empty for a body-only edit, whose
		// changed bodies are counted in `changed`.
		std::string EditOf(const std::string& before, const std::string& after, size_t* changed = nullptr)
		{
			const auto was = Parsed({ { "main.cf", before } }, nullptr);
			const auto is = Parsed({ { "main.cf", after } }, nullptr);
			const BodyEdit edit = FindBodyEdit(*was->GetCompilationUnit(1), *is->GetCompilationUnit(1));
			if (changed)
			{
				*changed = 0;
				for (const KeptDefinition& kept : edit.definitions) *changed += kept.bodyChanged ? 1 : 0;
			}
			return edit.refusal;
		}

		// A program built as an incremental build builds it - four partitions, the small functions copied - whose backend
		// stays for the edits after.
		std::string IrOf(llvm::Module& module)
		{
			std::string text;
			llvm::raw_string_ostream out(text);
			module.print(out, nullptr);
			return text;
		}

		// A program built as an incremental build builds it - four partitions, the small functions copied - whose backend
		// stays for the edits after; `ir` is each partition's as the last emission that emitted it left it.
		struct Build
		{
			std::unique_ptr<CompilerInstance> compiler;
			std::unique_ptr<llvm::LLVMContext> context;
			std::unique_ptr<ProgramBackend> backend;
			RetiredParses retired;
			std::vector<std::string> ir;
			std::string error;

			void KeepIr()
			{
				ir.resize(backend->PartitionCount());
				for (unsigned i = 0; i < backend->PartitionCount(); ++i)
					if (backend->Emitted(i)) ir[i] = IrOf(backend->PartitionModule(i));
			}
		};

		// `serving`: the backend keeps each partition's facts, and an edit's emission emits only the partitions it changes.
		std::unique_ptr<Build> Cold(const Files& files, bool serving = false)
		{
			auto build = std::make_unique<Build>();
			build->compiler = Parsed(files, nullptr);
			build->compiler->AnalyzeProgram();
			build->context = std::make_unique<llvm::LLVMContext>();
			build->backend = std::make_unique<ProgramBackend>(*build->context);
			build->backend->SplitInto(4, PartitionCopies::Small);
			if (serving) build->backend->KeepPartitionFacts();
			if (!build->backend->Lower(build->compiler->GetProgramModelForLowering(), "main", &build->error)) build->error += " (the cold build failed)";
			build->KeepIr();
			return build;
		}

		std::string IrOf(const Build& build)
		{
			std::string text;
			for (const std::string& partition : build.ir) text += partition;
			return text;
		}

		std::string Diagnostics(const ProgramBackend& backend)
		{
			std::string text;
			for (const CompilationError& error : backend.LoweringErrors()) text += std::string(error.message.c_str()) + "\n";
			return text;
		}

		// The edit to `files` served on `live` (SwapBodies, then EmitAgain): why it was refused, or empty.
		std::string Served(Build& live, const Files& files)
		{
			const auto edited = Parsed(files, live.compiler->Parses());
			if (std::string why = SwapBodies(*live.compiler, *edited, *live.backend, live.retired); !why.empty()) return why;
			std::vector<RetiredEmission> retired;
			std::string error;
			if (!live.backend->EmitAgain(live.compiler->GetProgramModelForLowering(), live.context, retired, &error, nullptr)) return "emitting failed: " + error;
			live.KeepIr();
			return {};
		}

		// Builds `before`, serves the edit to `after` on it, and checks it gives the cold build of `after`: its IR and its
		// lowering's diagnostics.
		// How the edit from `before` to `after` served on a build of `before` differs from a cold build of `after` - its
		// IR, its lowering's diagnostics, its reflection image - emitting every partition again and only the changed ones;
		// empty when it does not.
		std::string ColdBuildDiffers(const Files& before, const Files& after)
		{
			for (const bool serving : { false, true })
			{
				const std::string how = serving ? "emitting the changed partitions: " : "emitting every partition: ";
				const auto live = Cold(before, serving);
				const auto cold = Cold(after);
				if (!live->error.empty() || !cold->error.empty()) return how + "a cold build failed: " + live->error + cold->error;
				if (std::string why = Served(*live, after); !why.empty()) return how + "refused: " + why;
				if (IrOf(*live) != IrOf(*cold)) return how + "the IR differs";
				if (Diagnostics(*live->backend) != Diagnostics(*cold->backend)) return how + "the diagnostics differ";
				if (live->backend->ReflectionImageKey() != cold->backend->ReflectionImageKey()) return how + "the reflection image differs";
			}
			return {};
		}

		constexpr const char* kProgram =
			"int32 Twice(int32 v) { return v * 2; }\n"
			"int32 Thrice(int32 v) { return v * 3; }\n"
			"template<T> T Larger(T a, T b) { return a > b ? a : b; }\n"
			"int32 Pick(int32 a, int32 b) { return Larger<int32>(a, b); }\n"
			"int32 Edited(int32 v) { return v + 1; }\n"
			"int32 main() { return Twice(1) + Pick(2, 3) + Edited(4); }\n";

		Files With(const std::string& from, const std::string& to)
		{
			std::string text = kProgram;
			text.replace(text.find(from), from.size(), to);
			return { { "main.cf", text } };
		}
	}

	void BodySwapTest::AStatementEditIsABodyEdit()
	{
		size_t changed = 0;
		TEST_ASSERT_EQ(EditOf("int32 F(int32 v) { return v * 2; }\nint32 G() { return 1; }\n", "int32 F(int32 v) { return v * 3; }\nint32 G() { return 1; }\n", &changed), std::string());
		TEST_ASSERT_EQ(changed, size_t(1));
		TEST_PASSED();
	}

	void BodySwapTest::ALongerStatementOnItsLineIsABodyEdit()
	{
		size_t changed = 0;
		TEST_ASSERT_EQ(EditOf("int32 F() { return 2; }\nint32 G() { return 1; }\n", "int32 F() { return 2000 + 1; }\nint32 G() { return 1; }\n", &changed), std::string());
		TEST_ASSERT_EQ(changed, size_t(1));
		TEST_PASSED();
	}

	// The functions after a line a body gained move, and are lowered again where they are; anything else after it refuses.
	void BodySwapTest::AnAddedLineMovesWhatFollows()
	{
		size_t changed = 0;
		TEST_ASSERT_EQ(EditOf("int32 F()\n{\n\treturn 2;\n}\nint32 G() { return 1; }\n", "int32 F()\n{\n\tint32 x = 2;\n\treturn x;\n}\nint32 G() { return 1; }\n", &changed), std::string());
		TEST_ASSERT_EQ(changed, size_t(2));
		TEST_ASSERT_EQ(EditOf("int32 F() { return 2; } int32 G() { return 1; }\n", "int32 F() { return 20; } int32 G() { return 1; }\n", &changed), std::string());
		TEST_ASSERT_EQ(changed, size_t(2));
		TEST_ASSERT(!EditOf("int32 F()\n{\n\treturn 2;\n}\nint32 g_Value = 1;\n", "int32 F()\n{\n\tint32 x = 2;\n\treturn x;\n}\nint32 g_Value = 1;\n").empty());
		TEST_ASSERT(!EditOf("int32 F()\n{\n\treturn 2;\n}\nstruct S { public int32 a; }\n", "int32 F()\n{\n\n\treturn 2;\n}\nstruct S { public int32 a; }\n").empty());
		TEST_ASSERT(!EditOf("int32 F()\n{\n\treturn 2;\n}\ninline int32 G() { return 1; }\n", "int32 F()\n{\n\n\treturn 2;\n}\ninline int32 G() { return 1; }\n").empty());
		TEST_PASSED();
	}

	void BodySwapTest::ADeclarationEditIsNone()
	{
		TEST_ASSERT(!EditOf("int32 F(int32 v) { return v; }\n", "int32 F(int64 v) { return v; }\n").empty());
		TEST_ASSERT(!EditOf("struct S { public int32 a; }\n", "struct S { public int32 b; }\n").empty());
		TEST_PASSED();
	}

	void BodySwapTest::AnEditOutsideTheBodiesIsNone()
	{
		TEST_ASSERT(!EditOf("// one\nint32 F() { return 1; }\n", "// two\nint32 F() { return 1; }\n").empty());
		TEST_PASSED();
	}

	void BodySwapTest::ALambdaOrALocalTypeInAChangedBodyIsNone()
	{
		TEST_ASSERT(!EditOf("int32 F() { funcptr<int32()> f = []() -> int32 { return 1; }; return f(); }\n",
			"int32 F() { funcptr<int32()> f = []() -> int32 { return 2; }; return f(); }\n").empty());
		TEST_ASSERT(!EditOf("int32 F() { struct L { public int32 x; } l; l.x = 1; return l.x; }\n",
			"int32 F() { struct L { public int32 x; } l; l.x = 2; return l.x; }\n").empty());
		TEST_PASSED();
	}

	void BodySwapTest::ABodyThatDoesNotParseIsNone()
	{
		TEST_ASSERT(!EditOf("int32 F() { return 1; }\n", "int32 F() { return 1 +; }\n").empty());
		TEST_PASSED();
	}

	void BodySwapTest::AnInlineBodyIsNone()
	{
		TEST_ASSERT(!EditOf("inline int32 F() { return 1; }\n", "inline int32 F() { return 2; }\n").empty());
		TEST_PASSED();
	}

	void BodySwapTest::AStatementEditEmitsTheColdBuild()
	{
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kProgram } }, With("return v + 1;", "return v + 5;")), std::string());
		TEST_PASSED();
	}

	// A line added to Edited moves main and the lambda after it: both are lowered again where they are now, the lambda's
	// closure found by its new body.
	void BodySwapTest::AnAddedLineEmitsTheColdBuild()
	{
		const std::string lambda = "int32 Lambda() { funcptr<int32()> f = []() -> int32 { return 4; }; return f(); }\n";
		std::string before = kProgram;
		before.replace(before.find("int32 main()"), 0, lambda);
		std::string after = before;
		after.replace(after.find("return v + 1;"), 13, "\n\treturn v + 1;");
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", before } }, { { "main.cf", after } }), std::string());
		TEST_PASSED();
	}

	void BodySwapTest::ACallOfAnotherWrittenFunctionEmitsTheColdBuild()
	{
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kProgram } }, With("return v + 1;", "return Thrice(v) + 1;")), std::string());
		TEST_PASSED();
	}

	// Pick asked for Larger<int32> in the pass Edited lowers in: Edited may ask for it too.
	void BodySwapTest::AnInstanceMadeEarlierEmitsTheColdBuild()
	{
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kProgram } }, With("return v + 1;", "return Larger<int32>(v, 1);")), std::string());
		TEST_PASSED();
	}

	void BodySwapTest::ANewInstanceIsRefused()
	{
		const auto live = Cold({ { "main.cf", kProgram } });
		TEST_ASSERT_EQ(live->error, std::string());
		TEST_ASSERT(!Served(*live, With("return v + 1;", "return (int32)Larger<int64>(v, 1);")).empty());
		TEST_PASSED();
	}

	// Wide<int32> is lowered in the second pass, where it makes Larger<int64>: a first-pass body that asks for it now would
	// make it in the first pass in a cold build, before what the first pass's other bodies make (and need it first).
	void BodySwapTest::AnInstanceMadeInALaterPassIsRefused()
	{
		std::string program = kProgram;
		program.replace(program.find("int32 main()"), 0, "template<T> T Wide(T v) { return (T)Larger<int64>((int64)v, 1); }\nint32 UsesWide(int32 v) { return Wide<int32>(v); }\n");
		const auto live = Cold({ { "main.cf", program } });
		TEST_ASSERT_EQ(live->error, std::string());
		std::string edited = program;
		edited.replace(edited.find("return v + 1;"), 13, "return (int32)Larger<int64>(v, 1);");
		TEST_ASSERT(!Served(*live, { { "main.cf", edited } }).empty());
		TEST_PASSED();
	}

	void BodySwapTest::AnInstanceOnlyTheOldBodyAskedForIsRefused()
	{
		const Files before = With("return v + 1;", "return (int32)Larger<int64>(v, 1);");
		const auto live = Cold(before);
		TEST_ASSERT_EQ(live->error, std::string());
		TEST_ASSERT(!Served(*live, With("return v + 1;", "return v + 2;")).empty());
		TEST_PASSED();
	}

	void BodySwapTest::ABodyWithErrorsIsRefused()
	{
		const auto live = Cold({ { "main.cf", kProgram } });
		TEST_ASSERT_EQ(live->error, std::string());
		TEST_ASSERT(!Served(*live, With("return v + 1;", "return missing;")).empty());
		TEST_PASSED();
	}

	void BodySwapTest::AWarningIsReportedAsTheColdBuildReportsIt()
	{
		const Files warned = With("return v + 1;", "int64 wide = v; int32 narrow = wide; return narrow + 1;");
		const auto cold = Cold(warned);
		TEST_ASSERT(Diagnostics(*cold->backend).find("may change the value") != std::string::npos);
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kProgram } }, warned), std::string());
		TEST_ASSERT_EQ(ColdBuildDiffers(warned, With("return v + 1;", "int64 wide = v; int32 narrow = wide; return narrow + 2;")), std::string());
		TEST_PASSED();
	}

	// Partition 0 holds the program's own code, the others its functions: a statement edit emits the edited function's
	// partition (and those that copy it), never partition 0.
	void BodySwapTest::AnEditEmitsOnlyThePartitionsItChanges()
	{
		const auto live = Cold({ { "main.cf", kProgram } }, true);
		TEST_ASSERT_EQ(live->error, std::string());
		TEST_ASSERT_EQ(Served(*live, With("return v + 1;", "return v + 5;")), std::string());
		unsigned emitted = 0;
		for (unsigned i = 0; i < live->backend->PartitionCount(); ++i) emitted += live->backend->Emitted(i) ? 1 : 0;
		TEST_ASSERT(!live->backend->Emitted(0));
		TEST_ASSERT(emitted >= 1 && emitted < live->backend->PartitionCount());
		TEST_PASSED();
	}

	void BodySwapTest::TwoEditsInARowEmitTheColdBuild()
	{
		const auto live = Cold({ { "main.cf", kProgram } }, true);
		TEST_ASSERT_EQ(Served(*live, With("return v * 2;", "return v * 4;")), std::string());
		const Files twice = { { "main.cf", std::string(With("return v * 2;", "return v * 4;")[0].second).replace(std::string(kProgram).find("return v + 1;"), 13, "return v - 1;") } };
		TEST_ASSERT_EQ(Served(*live, twice), std::string());
		const auto cold = Cold(twice);
		TEST_ASSERT_EQ(cold->error, std::string());
		TEST_ASSERT(IrOf(*live) == IrOf(*cold));
		TEST_PASSED();
	}
} // namespace Testing
