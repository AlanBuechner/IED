// c4-outline: the declarations and syntax errors of C4 sources, through the compiler's C API (C4Api.h).
//
//   c4-outline [-D NAME[=VALUE]]... FILE...
//
// For each file: its name, its declarations as a tree (kind, name, line:column of the name), then its diagnostics in
// the form IED's and MSVC's matchers read, `FILE(LINE,COLUMN): error: MESSAGE`. Exit code 0 when no file has an error,
// 1 when one does, 2 for a usage error or a C4Api.dll older than the header this tool was built with.

#include "C4Api.h"
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	struct Arguments
	{
		std::vector<std::string> defines;
		std::vector<std::string> files;
	};

	bool ReadArguments(int argc, char** argv, Arguments& arguments)
	{
		for (int i = 1; i < argc; ++i)
		{
			const std::string word = argv[i];
			if (word == "-D" && i + 1 < argc) arguments.defines.push_back(argv[++i]);
			else if (word.rfind("-D", 0) == 0 && word.size() > 2) arguments.defines.push_back(word.substr(2));
			else if (!word.empty() && word[0] == '-') return false;
			else arguments.files.push_back(word);
		}
		return !arguments.files.empty();
	}

	int Depth(const c4a_parse* parse, const c4a_declaration& declaration)
	{
		int depth = 0;
		c4a_declaration up = declaration;
		while (up.parent >= 0 && c4a_declaration_at(parse, static_cast<uint32_t>(up.parent), &up)) ++depth;
		return depth;
	}

	void PrintDeclarations(const c4a_parse* parse)
	{
		for (uint32_t i = 0; i < c4a_declaration_count(parse); ++i)
		{
			c4a_declaration declaration;
			c4a_declaration_at(parse, i, &declaration);
			const std::string indent(static_cast<size_t>(2 + 2 * Depth(parse, declaration)), ' ');
			std::printf("%s%s %s (%u:%u)\n", indent.c_str(), c4a_kind_name(declaration.kind), declaration.name, declaration.name_at.line, declaration.name_at.column);
		}
	}

	// Prints the file's diagnostics; whether one is an error.
	bool PrintDiagnostics(const c4a_parse* parse, const std::string& file)
	{
		bool error = false;
		for (uint32_t i = 0; i < c4a_diagnostic_count(parse); ++i)
		{
			c4a_diagnostic diagnostic;
			c4a_diagnostic_at(parse, i, &diagnostic);
			error = error || diagnostic.severity == C4A_ERROR;
			std::printf("%s(%u,%u): %s: %s\n", file.c_str(), diagnostic.at.line, diagnostic.at.column, diagnostic.severity == C4A_ERROR ? "error" : "warning", diagnostic.message);
		}
		return error;
	}

	// Outlines one file; whether it has an error.
	bool Outline(const std::string& file, const std::vector<const char*>& defines)
	{
		c4a_parse_options options = {};
		options.path = file.c_str();
		options.defines = defines.data();
		options.define_count = static_cast<uint32_t>(defines.size());
		c4a_parse* parse = c4a_parse_source(&options);
		std::printf("%s\n", file.c_str());
		PrintDeclarations(parse);
		const bool error = PrintDiagnostics(parse, file) || !c4a_parse_read(parse);
		c4a_parse_destroy(parse);
		return error;
	}
}

int main(int argc, char** argv)
{
	Arguments arguments;
	if (!ReadArguments(argc, argv, arguments))
	{
		std::fprintf(stderr, "usage: c4-outline [-D NAME[=VALUE]]... FILE...\n");
		return 2;
	}
	if (c4a_api_version() < C4A_API_VERSION)
	{
		std::fprintf(stderr, "c4-outline: C4Api.dll implements API %d, this tool needs %d (compiler %s)\n", c4a_api_version(), C4A_API_VERSION, c4a_compiler_version());
		return 2;
	}
	std::vector<const char*> defines;
	for (const std::string& define : arguments.defines) defines.push_back(define.c_str());
	bool failed = false;
	for (const std::string& file : arguments.files) failed = Outline(file, defines) || failed;
	return failed ? 1 : 0;
}
