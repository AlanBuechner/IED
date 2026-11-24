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

		// The refusal of the edit of `main.cf` from `before` to `after` (FindFileEdit); empty for an edit of bodies and
		// values, whose changed bodies are counted in `changed` and changed globals in `values`.
		std::string EditOf(const std::string& before, const std::string& after, size_t* changed = nullptr, size_t* values = nullptr)
		{
			const auto was = Parsed({ { "main.cf", before } }, nullptr);
			const auto is = Parsed({ { "main.cf", after } }, nullptr);
			const FileEdit edit = FindFileEdit(*was->GetCompilationUnit(1), *is->GetCompilationUnit(1));
			if (changed)
			{
				*changed = 0;
				for (const KeptDefinition& kept : edit.definitions) *changed += kept.bodyChanged ? 1 : 0;
			}
			if (values)
			{
				*values = 0;
				for (const KeptGlobal& kept : edit.globals) *values += kept.valueChanged ? 1 : 0;
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

		// The edit to `files` served on `live` (SwapEdits, then EmitAgain): why it was refused, or empty.
		std::string Served(Build& live, const Files& files)
		{
			const auto edited = Parsed(files, live.compiler->Parses());
			if (std::string why = SwapEdits(*live.compiler, *edited, *live.backend, live.retired); !why.empty()) return why;
			std::vector<RetiredEmission> retired;
			std::string error;
			if (!live.backend->EmitAgain(live.compiler->GetProgramModelForLowering(), live.context, retired, &error, nullptr)) return "emitting failed: " + error;
			live.KeepIr();
			return {};
		}

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

		// Globals of each kind a value edit serves, and the bodies that read them: a template's instance, an inline function,
		// plain functions.
		constexpr const char* kValues =
			"const int32 c_Scale = 2;\n"
			"const int32 c_Twice = c_Scale * 2;\n"
			"int32 g_Count = 5;\n"
			"const char* c_Name = \"name\";\n"
			"class Settings\n{\n\tpublic static const int32 k_Limit = 7;\n}\n"
			"template<T> T Scaled(T v) { return v * (T)c_Scale; }\n"
			"inline int32 Twice() { return c_Twice; }\n"
			"int32 UsesScale(int32 v) { return v * c_Scale; }\n"
			"int32 UsesCount() { return g_Count; }\n"
			"int32 UsesName() { return (int32)c_Name[0]; }\n"
			"int32 UsesLimit() { return Settings::k_Limit; }\n"
			"int32 Other(int32 v) { return v + 1; }\n"
			"int32 main() { return UsesScale(1) + Scaled<int32>(2) + Twice() + UsesCount() + UsesName() + UsesLimit() + Other(3); }\n";

		std::string Replaced(std::string text, const std::string& from, const std::string& to)
		{
			text.replace(text.find(from), from.size(), to);
			return text;
		}

		Files Values(const std::string& from, const std::string& to)
		{
			return { { "main.cf", Replaced(kValues, from, to) } };
		}

		// Why the edit of main.cf from `before` to `after` is not served when both build cold: "(served)" when it is, and
		// "(no cold build)" when one does not build.
		std::string RefusalOf(const std::string& before, const std::string& after)
		{
			const auto live = Cold({ { "main.cf", before } }, true);
			const auto cold = Cold({ { "main.cf", after } });
			if (!live->error.empty() || !cold->error.empty()) return "(no cold build) " + live->error + cold->error;
			const std::string why = Served(*live, { { "main.cf", after } });
			return why.empty() ? "(served)" : why;
		}

		// `expected` when `why` says it, else all of `why`.
		std::string Says(const std::string& why, const std::string& expected)
		{
			return why.find(expected) != std::string::npos ? expected : why;
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
		TEST_ASSERT(!EditOf("int32 F()\n{\n\treturn 2;\n}\n[compile_time] int32 G() { return 1; }\n", "int32 F()\n{\n\n\treturn 2;\n}\n[compile_time] int32 G() { return 1; }\n").empty());
		TEST_ASSERT_EQ(EditOf("int32 F()\n{\n\treturn 2;\n}\ninline int32 G() { return 1; }\n", "int32 F()\n{\n\n\treturn 2;\n}\ninline int32 G() { return 1; }\n", &changed), std::string());
		TEST_ASSERT_EQ(changed, size_t(2));
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

	// Other partitions copy an inline body from its function, lowered again, and a template's lowered instances are lowered
	// again; a [compile_time] function's is run by constants.
	void BodySwapTest::AnInlineBodyIsABodyEdit()
	{
		size_t changed = 0;
		TEST_ASSERT_EQ(EditOf("inline int32 F() { return 1; }\n", "inline int32 F() { return 2; }\n", &changed), std::string());
		TEST_ASSERT_EQ(changed, size_t(1));
		TEST_ASSERT_EQ(EditOf("template<T> T F(T v) { return v; }\n", "template<T> T F(T v) { return v + v; }\n", &changed), std::string());
		TEST_ASSERT_EQ(changed, size_t(1));
		TEST_ASSERT(!EditOf("[compile_time] int32 F() { return 1; }\n", "[compile_time] int32 F() { return 2; }\n").empty());
		TEST_ASSERT(!EditOf("template<T> T F(T v) { return v; }\n", "template<T> T F(T v, T w) { return v; }\n").empty());
		TEST_PASSED();
	}

	// Each instance the build lowered is lowered again from the template's new body.
	void BodySwapTest::ATemplatesBodyEmitsTheColdBuild()
	{
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kProgram } }, With("a > b ? a : b", "a >= b ? a : b")), std::string());
		// A line added before the template moves it: its instances are lowered again where it is now.
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kProgram } }, With("{ return v * 2; }", "{\n\treturn v * 2;\n}")), std::string());
		const std::string box = "template<T> class Box\n{\n\tpublic T v;\n\tpublic T Get() { return v; }\n\tpublic T Twice() { return v + v; }\n}\n"
			"int32 main() { Box<int32> a; a.v = 2; Box<int64> b; b.v = 3; return a.Get() + (int32)b.Get(); }\n";
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", box } }, { { "main.cf", Replaced(box, "T Get() { return v; }", "T Get() { return v + (T)1; }") } }), std::string());
		TEST_PASSED();
	}

	void BodySwapTest::AnInlineBodyEmitsTheColdBuild()
	{
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kValues } }, Values("return c_Twice;", "return c_Twice + 3;")), std::string());
		TEST_PASSED();
	}

	void BodySwapTest::AGlobalsValueIsAValueEdit()
	{
		size_t changed = 0, values = 0;
		TEST_ASSERT_EQ(EditOf("const int32 c_A = 1;\nint32 F() { return c_A; }\n", "const int32 c_A = 2;\nint32 F() { return c_A; }\n", &changed, &values), std::string());
		TEST_ASSERT_EQ(changed, size_t(0));
		TEST_ASSERT_EQ(values, size_t(1));
		// A longer value moves the end of its own definition alone.
		TEST_ASSERT_EQ(EditOf("const int32 c_A = 9;\nint32 F() { return c_A; }\n", "const int32 c_A = 10;\nint32 F() { return c_A; }\n", &changed, &values), std::string());
		TEST_ASSERT_EQ(values, size_t(1));
		TEST_ASSERT_EQ(EditOf("class S { public static const int32 k = 1; }\n", "class S { public static const int32 k = 2; }\n", &changed, &values), std::string());
		TEST_ASSERT_EQ(values, size_t(1));
		TEST_ASSERT_EQ(EditOf("class S\n{\n\tpublic static const int32 k = 1;\n}\n", "class S\n{\n\tpublic static const int32 k = 1000;\n}\n", &changed, &values), std::string());
		TEST_ASSERT_EQ(values, size_t(1));
		// A value and a body in one edit.
		TEST_ASSERT_EQ(EditOf("int32 g = 1;\nint32 F() { return 1; }\n", "int32 g = 2;\nint32 F() { return 3; }\n", &changed, &values), std::string());
		TEST_ASSERT_EQ(changed, size_t(1));
		TEST_ASSERT_EQ(values, size_t(1));
		TEST_PASSED();
	}

	// What else a value edit could change is a declaration: a field's initializer, a global's type, another declaration
	// moved along a line.
	void BodySwapTest::AValueOfADeclarationIsNone()
	{
		TEST_ASSERT(!EditOf("struct S { public int32 a = 1; }\n", "struct S { public int32 a = 2; }\n").empty());
		TEST_ASSERT(!EditOf("const int32 a = 1;\n", "const int64 a = 1;\n").empty());
		TEST_ASSERT(!EditOf("const int32 a = 9; const int32 b = 1;\n", "const int32 a = 10; const int32 b = 1;\n").empty());
		// A class that ends on the line its static member's longer value moves.
		TEST_ASSERT(!EditOf("class S { public static const int32 k = 1; }\n", "class S { public static const int32 k = 10; }\n").empty());
		TEST_PASSED();
	}

	void BodySwapTest::AConstantsValueEmitsTheColdBuild()
	{
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kValues } }, Values("c_Scale = 2;", "c_Scale = 3;")), std::string());
		TEST_PASSED();
	}

	// A value folded from another, a global's starting value, a string, a static member's value: each emits the cold build.
	void BodySwapTest::EveryKindOfValueEmitsTheColdBuild()
	{
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kValues } }, Values("c_Scale * 2", "c_Scale * 5")), std::string());
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kValues } }, Values("g_Count = 5;", "g_Count = 600;")), std::string());
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kValues } }, Values("\"name\"", "\"another\"")), std::string());
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kValues } }, Values("k_Limit = 7;", "k_Limit = 70;")), std::string());
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kValues } }, Values("g_Count = 5;", "g_Count = 5 + c_Scale;")), std::string());
		TEST_PASSED();
	}

	void BodySwapTest::AValueAndABodyInOneEditEmitTheColdBuild()
	{
		const Files after = { { "main.cf", Replaced(Replaced(kValues, "c_Scale = 2;", "c_Scale = 4;"), "return v + 1;", "return v + 2;") } };
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", kValues } }, after), std::string());
		TEST_PASSED();
	}

	// A comment naming the constant reads nothing, nor does another constant of its name where it is declared (its
	// reader is lowered again too, the same).
	void BodySwapTest::AValueACommentNamesIsServed()
	{
		const std::string program = "// c_N counts\nconst int32 c_N = 2;\nint32 main() { return c_N; }\n";
		TEST_ASSERT_EQ(RefusalOf(program, Replaced(program, "c_N = 2", "c_N = 3")), std::string("(served)"));
		const std::string twoOfAName = "namespace A { const int32 c_V = 1; }\nnamespace B { const char* c_V = \"x\"; }\n"
			"int32 FromA() { return A::c_V; }\nint32 FromB() { return (int32)B::c_V[0]; }\nint32 main() { return FromA() + FromB(); }\n";
		TEST_ASSERT_EQ(RefusalOf(twoOfAName, Replaced(twoOfAName, "c_V = 1", "c_V = 2")), std::string("(served)"));
		TEST_ASSERT_EQ(ColdBuildDiffers({ { "main.cf", twoOfAName } }, { { "main.cf", Replaced(twoOfAName, "c_V = 1", "c_V = 2") } }), std::string());
		TEST_PASSED();
	}

	// A value read where no body lowered again reads it - an array's extent, a parameter's default, a [compile_time]
	// function - or that no longer folds is a cold build. (A macro's: ToolsTest's served builds, which run the macro pass.)
	void BodySwapTest::AValueReadOutsideABodyIsRefused()
	{
		const std::string extent = "const int32 c_N = 2;\nint32[c_N] g_Table;\nint32 main() { return g_Table[0]; }\n";
		TEST_ASSERT_EQ(Says(RefusalOf(extent, Replaced(extent, "c_N = 2", "c_N = 3")), "outside a function body"), std::string("outside a function body"));
		const std::string defaulted = "const int32 c_N = 2;\nint32 F(int32 v = c_N) { return v; }\nint32 main() { return F(); }\n";
		TEST_ASSERT_EQ(Says(RefusalOf(defaulted, Replaced(defaulted, "c_N = 2", "c_N = 3")), "outside a function body"), std::string("outside a function body"));
		const std::string compileTime = "const int32 c_N = 2;\n[compile_time] int32 Squared() { return c_N * c_N; }\nconst int32 c_S = Squared();\nint32 main() { return c_S; }\n";
		TEST_ASSERT_EQ(Says(RefusalOf(compileTime, Replaced(compileTime, "c_N = 2", "c_N = 3")), "[compile_time]"), std::string("[compile_time]"));
		const std::string folded = "const int32 c_N = 2;\nint32 Seed() { return 3; }\nint32 main() { return c_N; }\n";
		TEST_ASSERT_EQ(Says(RefusalOf(folded, Replaced(folded, "c_N = 2", "c_N = Seed()")), "no longer folds"), std::string("no longer folds"));
		TEST_PASSED();
	}

	// A starting value lives in its global's partition alone: the edit emits that one, not partition 0.
	void BodySwapTest::AValueEditEmitsOnlyThePartitionsItChanges()
	{
		const auto live = Cold({ { "main.cf", kValues } }, true);
		TEST_ASSERT_EQ(live->error, std::string());
		TEST_ASSERT_EQ(Served(*live, Values("g_Count = 5;", "g_Count = 6;")), std::string());
		unsigned emitted = 0;
		for (unsigned i = 0; i < live->backend->PartitionCount(); ++i) emitted += live->backend->Emitted(i) ? 1 : 0;
		TEST_ASSERT(!live->backend->Emitted(0));
		TEST_ASSERT(emitted >= 1 && emitted < live->backend->PartitionCount());
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
