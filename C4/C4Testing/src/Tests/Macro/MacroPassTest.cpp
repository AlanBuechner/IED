#include "MacroPassTest.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/SourceMacros.h"
#include "Macro/MacroPass.h"
#include <chrono>
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// `include "x"` lines resolve by name among the inputs.
		std::string ByName(const std::string&, const std::string& include) { return "/" + include; }

		MacroPass::Input File(const std::string& name, const std::string& text)
		{
			MacroPass::Input input;
			input.absPath = "/" + name;
			input.name = name;
			input.text = text;
			return input;
		}

		const MacroPass::FileResult* ResultOf(const std::vector<MacroPass::FileResult>& results, const std::string& name)
		{
			for (const MacroPass::FileResult& result : results)
				if (result.name == name) return &result;
			return nullptr;
		}
	}

	void MacroPassTest::IncludesCarryTheirMacros()
	{
		std::vector<MacroPass::Input> files = { File("a.ch", "#define SIZE 4\n"), File("main.cf", "include \"a.ch\"\nint32 g = SIZE;\n") };
		const std::vector<MacroPass::FileResult> results = MacroPass::RunFiles(std::move(files), ByName);
		const MacroPass::FileResult* main = ResultOf(results, "main.cf");
		TEST_ASSERT(main != nullptr && main->text.find("int32 g = 4;") != std::string::npos);
		TEST_ASSERT(ResultOf(results, "a.ch")->exported.Has("SIZE"));
		TEST_PASSED();
	}

	// BUG-239: each file expanded every header it reached again, so a diamond of headers (each level including two
	// files of the next) took time exponential in its depth; expanded once each, it is linear.
	void MacroPassTest::AHeaderIsExpandedOnce()
	{
		const int kLevels = 24;
		std::vector<MacroPass::Input> files;
		for (int level = 0; level < kLevels; ++level)
		{
			const std::string next = std::to_string(level + 1);
			const std::string text = level + 1 < kLevels
				? "include \"l" + next + "a.ch\"\ninclude \"l" + next + "b.ch\"\n"
				: "#define DEEPEST 42\n";
			files.push_back(File("l" + std::to_string(level) + "a.ch", text));
			files.push_back(File("l" + std::to_string(level) + "b.ch", text));
		}
		files.push_back(File("main.cf", "include \"l0a.ch\"\nint32 g = DEEPEST;\n"));
		const auto start = std::chrono::steady_clock::now();
		const std::vector<MacroPass::FileResult> results = MacroPass::RunFiles(std::move(files), ByName);
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		TEST_ASSERT(seconds < 5.0);
		TEST_ASSERT(ResultOf(results, "main.cf")->text.find("int32 g = 42;") != std::string::npos);
		TEST_ASSERT_EQ(results.size(), (size_t)(2 * kLevels + 1));
		TEST_PASSED();
	}

	// BUG-237: a source given as text kept its directives and macros unexpanded.
	void MacroPassTest::SourcesGivenAsTextAreExpanded()
	{
		CompilerInstance compiler;
		SourceManager& sources = compiler.GetSourceManager();
		const uint32 header = sources.RegisterInMemory("limits.ch", "#define LIMIT 9\n");
		const uint32 main = sources.RegisterInMemory("main.cf", "include \"limits.ch\"\n#if LIMIT > 5\nint32 g = LIMIT;\n#endif\n");
		const std::vector<MacroPass::FileResult> results = ExpandSourceMacros(sources, {});
		TEST_ASSERT_EQ(results.size(), (size_t)2);
		const MacroPass::FileResult* expanded = nullptr;
		for (const MacroPass::FileResult& result : results)
			if (result.absPath == MacroIdentity(sources.GetInfo(main))) expanded = &result;
		TEST_ASSERT(expanded != nullptr);
		TEST_ASSERT(expanded->text.find("int32 g = 9;") != std::string::npos);
		TEST_ASSERT(expanded->text.find("#if") == std::string::npos);
		TEST_ASSERT(MacroIdentity(sources.GetInfo(header)) == "memory:" + std::to_string(header));
		TEST_PASSED();
	}

	void MacroPassTest::InterfaceFilesPassThroughWithTheirMacros()
	{
		CompilerInstance compiler;
		SourceManager& sources = compiler.GetSourceManager();
		// An interface file: its text is final (already expanded in its host) and it exports SQUARE.
		const uint32 header = sources.RegisterInMemory("math.ch", "int32 Area(int32 side) ;\n");
		const uint32 script = sources.RegisterInMemory("script.cf", "include \"math.ch\"\nint32 g = SQUARE(3);\n");
		MacroTable exports;
		exports.DefineFunction("SQUARE", { "x" }, "((x) * (x))");
		const std::vector<MacroPass::FileResult> results = ExpandSourceMacros(sources, {}, { { header, exports } });
		for (const MacroPass::FileResult& result : results)
		{
			if (result.absPath == MacroIdentity(sources.GetInfo(header)))
			{
				TEST_ASSERT(result.text == "int32 Area(int32 side) ;\n");
				TEST_ASSERT(result.exported.Has("SQUARE"));
			}
			if (result.absPath == MacroIdentity(sources.GetInfo(script)))
				TEST_ASSERT(result.text.find("int32 g = ((3) * (3));") != std::string::npos);
		}
		TEST_PASSED();
	}
} // namespace Testing
