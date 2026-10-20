#include "MacroPreprocessorTest.h"
#include "Macro/MacroPreprocessor.h"
#include <algorithm>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		struct Run
		{
			PreprocessedFile file;
			MacroTable table;
			std::vector<std::string> includes;
		};

		Run Process(const std::string& text)
		{
			Run run;
			run.file = MacroPreprocessor::Process(text, "t.cf", run.table,
				[&](const std::string& path) { run.includes.push_back(path); });
			return run;
		}

		// The non-blank lines of the output, trimmed.
		std::vector<std::string> Code(const PreprocessedFile& file)
		{
			std::vector<std::string> lines;
			size_t start = 0;
			while (start <= file.text.size())
			{
				const size_t eol = file.text.find('\n', start);
				std::string line = file.text.substr(start, (eol == std::string::npos ? file.text.size() : eol) - start);
				const size_t b = line.find_first_not_of(" \t");
				if (b != std::string::npos) lines.push_back(line.substr(b, line.find_last_not_of(" \t") - b + 1));
				if (eol == std::string::npos) break;
				start = eol + 1;
			}
			return lines;
		}

		bool Reports(const PreprocessedFile& file, unsigned line, const std::string& message, ErrorSeverity severity = ErrorSeverity::Error)
		{
			return std::any_of(file.diagnostics.begin(), file.diagnostics.end(), [&](const MacroDiagnostic& d) {
				return d.line == line && d.severity == severity && d.message.find(message) == 0;
			});
		}
	}

	void MacroPreprocessorTest::CommentsHideDirectives()
	{
		const Run run = Process("/*\n#define HIDDEN 1\n*/\n#ifdef HIDDEN\nbad\n#endif\nint x; /* a\n#undef X\n b */ int y;\n");
		TEST_ASSERT(!run.table.Has("HIDDEN"));
		TEST_ASSERT(Code(run.file) == (std::vector<std::string>{ "/*", "#define HIDDEN 1", "*/", "int x; /* a", "#undef X", "b */ int y;" }));
		const Run inString = Process("const char* s = \"/*\";\n#define AFTER 1\nAFTER\n");
		TEST_ASSERT(inString.table.Has("AFTER") && Code(inString.file).back() == "1");
		TEST_ASSERT(inString.file.diagnostics.empty());
	}

	void MacroPreprocessorTest::ObjectAndFunctionLikeDefinitions()
	{
		const Run run = Process("#define FLAG (1 << 3)\n#define F(x) [x]\n#define Z() 7\n#define EMPTY\nFLAG F(2) Z() EMPTY.\n");
		TEST_ASSERT(!run.table.Get("FLAG")->isFunctionLike);           // BUG-126: a space before `(`
		TEST_ASSERT(run.table.Get("F")->isFunctionLike && run.table.Get("F")->params.size() == 1);
		TEST_ASSERT(run.table.Get("Z")->isFunctionLike && run.table.Get("Z")->params.empty());
		TEST_ASSERT(Code(run.file) == (std::vector<std::string>{ "(1 << 3) [2] 7 ." }));
		TEST_ASSERT(run.file.diagnostics.empty());
		const Run redefined = Process("#define A 1\n#define A 1\n#define A 2\n#undef A\n#define A 3\n");
		TEST_ASSERT(redefined.file.diagnostics.size() == 1);
		TEST_ASSERT(Reports(redefined.file, 3, "'A' is redefined: #undef it first", ErrorSeverity::Warning));
	}

	void MacroPreprocessorTest::DefinitionErrors()
	{
		TEST_ASSERT(Reports(Process("#define\n").file, 1, "#define needs a macro name"));
		TEST_ASSERT(Reports(Process("#define 1X\n").file, 1, "#define needs a macro name"));
		TEST_ASSERT(Reports(Process("#define defined 1\n").file, 1, "'defined' cannot be defined as a macro"));
		TEST_ASSERT(Reports(Process("#define __LINE__ 1\n").file, 1, "'__LINE__' cannot be defined as a macro"));
		TEST_ASSERT(Reports(Process("#define X+1\n").file, 1, "a space must follow the macro name 'X'"));
		TEST_ASSERT(Reports(Process("#define F(a, 2) a\n").file, 1, "the parameters of 'F' must be names separated by commas"));
		TEST_ASSERT(Reports(Process("#define F(a, a) a\n").file, 1, "'a' is a parameter of 'F' twice"));
		TEST_ASSERT(Reports(Process("#define F(..., a) a\n").file, 1, "'...' must be the last parameter of 'F'"));
		TEST_ASSERT(Reports(Process("#define F(a\n").file, 1, "the parameter list of 'F' has no ')'"));
		TEST_ASSERT(Reports(Process("#define F(a) #b\n").file, 1, "'#' in the macro 'F' must be followed by one of its parameters"));
		TEST_ASSERT(Reports(Process("#define F(a) ## a\n").file, 1, "'##' cannot start or end the body of the macro 'F'"));
		TEST_ASSERT(Reports(Process("#define G a ##\n").file, 1, "'##' cannot start or end the body of the macro 'G'"));
		TEST_ASSERT(Reports(Process("#undef\n").file, 1, "#undef needs a macro name"));
		TEST_ASSERT(Reports(Process("#undef A B\n").file, 1, "unexpected 'B' after the macro name of #undef"));
		TEST_ASSERT(Reports(Process("#define F(x) x\nF(1, 2)\n").file, 2, "the macro 'F' takes 1 argument, but 2 were given"));
	}

	void MacroPreprocessorTest::ContinuationsAndCallsAcrossLines()
	{
		const Run run = Process("#define ADD(a, b) \\\n\t((a) + (b))\n#if 1 && \\\n 1\nint x = ADD(1,\n\t2); // done\n#endif\n");
		TEST_ASSERT(run.table.Get("ADD")->body == "((a) + (b))");
		TEST_ASSERT(Code(run.file) == (std::vector<std::string>{ "int x = ((1) + (2));" }));
		TEST_ASSERT(run.file.diagnostics.empty());
		// A comment inside a call does not swallow the line after it.
		const Run commented = Process("#define ADD(a, b) a+b\nADD(1, // one\n2)\n");
		TEST_ASSERT(Code(commented.file) == (std::vector<std::string>{ "1+2" }));
	}

	void MacroPreprocessorTest::EveryLineStaysInPlace()
	{
		const std::string text = "#define A \\\n 1\nA\n#if 0\nx\n#else\ny\n#endif\n#define F(a, b) a b\nF(1,\n2)\n__LINE__\n";
		const Run run = Process(text);
		TEST_ASSERT(std::count(run.file.text.begin(), run.file.text.end(), '\n') == std::count(text.begin(), text.end(), '\n'));
		const std::vector<std::string> code = Code(run.file);
		TEST_ASSERT(code == (std::vector<std::string>{ "1", "y", "1 2", "12" }));
		// A trailing comment is not part of a macro body.
		TEST_ASSERT(Process("#define B 2 // two\nB\n").table.Get("B")->body == "2");
		TEST_ASSERT(Process("#define C 3 /* three */\n").table.Get("C")->body == "3");
	}

	void MacroPreprocessorTest::ConditionalGroups()
	{
		const Run run = Process(
			"#define TWO 2\n"
			"#if TWO == 2 && defined(TWO)\na\n#elif 1\nb\n#else\nc\n#endif\n"
			"#if 0\n#if 1 / 0\nnever\n#endif\n#elif TWO > 1\nd\n#endif\n"
			"#ifndef NOPE\ne\n#endif\n"
			"#if 0\n#error not reached\n#define NOT_DEFINED 1\n#endif\n");
		TEST_ASSERT(Code(run.file) == (std::vector<std::string>{ "a", "d", "e" }));
		TEST_ASSERT(run.file.diagnostics.empty());                  // a skipped #if is never evaluated
		TEST_ASSERT(!run.table.Has("NOT_DEFINED"));
	}

	void MacroPreprocessorTest::MisplacedConditionalsAreErrors()
	{
		TEST_ASSERT(Reports(Process("#else\n").file, 1, "#else with no #if before it"));
		TEST_ASSERT(Reports(Process("#elif 1\n").file, 1, "#elif with no #if before it"));
		TEST_ASSERT(Reports(Process("#endif\n").file, 1, "#endif with no #if before it"));
		TEST_ASSERT(Reports(Process("#if 1\n#else\n#else\n#endif\n").file, 3, "a second #else for the #if on line 1"));
		TEST_ASSERT(Reports(Process("#if 1\n#else\n#elif 1\n#endif\n").file, 3, "#elif after the #else of the #if on line 1"));
		TEST_ASSERT(Reports(Process("x\n#if 1\n").file, 2, "this #if has no #endif"));
		TEST_ASSERT(Reports(Process("#if 1\n#endif FOO\n").file, 2, "unexpected 'FOO' after #endif"));
		TEST_ASSERT(Reports(Process("#ifdef\n#endif\n").file, 1, "#ifdef needs a macro name"));
		TEST_ASSERT(Reports(Process("#if 1 / 0\n#endif\n").file, 1, "division by zero in a #if condition"));
	}

	void MacroPreprocessorTest::OtherDirectives()
	{
		TEST_ASSERT(Reports(Process("#error stop here\n").file, 1, "#error stop here"));
		TEST_ASSERT(Reports(Process("#warning careful\n").file, 1, "#warning careful", ErrorSeverity::Warning));
		TEST_ASSERT(Process("#pragma once\n#pragma anything at all\n#\n").file.diagnostics.empty());
		TEST_ASSERT(Reports(Process("#line 10\n").file, 1, "#line is not supported"));
		TEST_ASSERT(Reports(Process("#include \"a.ch\"\n").file, 1, "'#include' is not a C4 directive"));
		TEST_ASSERT(Reports(Process("  #bogus\n").file, 1, "unknown directive '#bogus'"));
		TEST_ASSERT(Process("#if 0\n#bogus\n#endif\n").file.diagnostics.empty()); // skipped
	}

	void MacroPreprocessorTest::IncludesInheritMacros()
	{
		const Run run = Process("include \"a.ch\"\n// include \"b.ch\"\n  include \"c.ch\"\nincludeX \"d.ch\"\n");
		TEST_ASSERT(run.includes == (std::vector<std::string>{ "a.ch", "c.ch" }));
	}
} // namespace Testing
