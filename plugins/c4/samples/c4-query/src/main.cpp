// c4-query: what the compiler knows of a C4 program, through version 2 of its C API (C4Api.h).
//
//   c4-query ROOT diagnostics
//   c4-query ROOT resolve FILE LINE COLUMN       what the name there is: kind, qualified name, where it is declared
//   c4-query ROOT type FILE LINE COLUMN          the type of the expression or declaration there
//   c4-query ROOT references FILE LINE COLUMN    every use of the declaration there, its own name first
//   c4-query ROOT members QUALIFIED_NAME         the fields and methods of an object type
//
// ROOT is the folder of the program's C4 files; FILE is named from it. Diagnostics print in the form IED's and MSVC's
// matchers read, `FILE(LINE,COLUMN): error: MESSAGE`. Exit code 0 on success, 1 when a query finds nothing or the program
// has an error, 2 for a usage error or a C4Api.dll older than the header this tool was built with.

#include "C4Api.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

namespace
{
	int Usage()
	{
		std::fprintf(stderr, "usage: c4-query ROOT diagnostics | resolve|type|references FILE LINE COLUMN | members QUALIFIED_NAME\n");
		return 2;
	}

	int PrintDiagnostics(const c4a_program* program)
	{
		int errors = 0;
		for (uint32_t i = 0; i < c4a_program_diagnostic_count(program); ++i)
		{
			c4a_program_diagnostic diagnostic;
			c4a_program_diagnostic_at(program, i, &diagnostic);
			errors += diagnostic.severity == C4A_ERROR ? 1 : 0;
			std::printf("%s(%u,%u): %s: %s\n", diagnostic.file, diagnostic.at.line, diagnostic.at.column, diagnostic.severity == C4A_ERROR ? "error" : "warning", diagnostic.message);
		}
		return errors ? 1 : 0;
	}

	void PrintLocation(const char* label, const c4a_location& location)
	{
		std::printf("%s%s(%u,%u)\n", label, location.file, location.start.line, location.start.column);
	}

	int Resolve(c4a_program* program, const char* file, c4a_position at)
	{
		c4a_symbol symbol;
		if (!c4a_program_resolve(program, file, at, &symbol))
			return 1;
		std::printf("%s %s\n", c4a_kind_name(symbol.kind), symbol.name);
		std::printf("  declaration: %s\n", symbol.detail);
		if (symbol.type[0]) std::printf("  type: %s\n", symbol.type);
		PrintLocation("  declared at: ", symbol.declared);
		return 0;
	}

	int Type(c4a_program* program, const char* file, c4a_position at)
	{
		const char* type = c4a_program_type_at(program, file, at);
		if (!type)
			return 1;
		std::printf("%s\n", type);
		return 0;
	}

	int References(c4a_program* program, const char* file, c4a_position at)
	{
		c4a_locations* uses = c4a_program_references(program, file, at, 1);
		if (!uses)
			return 1;
		std::vector<std::tuple<std::string, uint32_t, uint32_t>> places; // the order the program's files come in is the compiler's: print them in the reader's
		for (uint32_t i = 0; i < c4a_locations_count(uses); ++i)
		{
			c4a_location location;
			c4a_locations_at(uses, i, &location);
			places.emplace_back(location.file, location.start.line, location.start.column);
		}
		std::sort(places.begin(), places.end());
		for (const auto& [file, line, column] : places)
			std::printf("%s(%u,%u)\n", file.c_str(), line, column);
		c4a_locations_destroy(uses);
		return 0;
	}

	int Members(c4a_program* program, const char* name)
	{
		c4a_members* members = c4a_program_members_of(program, name);
		if (!members)
			return 1;
		for (uint32_t i = 0; i < c4a_members_count(members); ++i)
		{
			c4a_member member;
			c4a_members_at(members, i, &member);
			std::printf("%s %s %s%s: %s (%s:%u)\n", member.access, c4a_kind_name(member.kind), member.is_static ? "static " : "", member.name, member.signature, member.declared.file, member.declared.start.line);
		}
		c4a_members_destroy(members);
		return 0;
	}

	bool ReadPosition(char** argv, c4a_position& at)
	{
		at.line = static_cast<uint32_t>(std::atoi(argv[4]));
		at.column = static_cast<uint32_t>(std::atoi(argv[5]));
		return at.line != 0 && at.column != 0;
	}

	int Query(c4a_program* program, int argc, char** argv)
	{
		const std::string verb = argv[2];
		c4a_position at = {};
		if (verb == "diagnostics" && argc == 3) return PrintDiagnostics(program);
		if (verb == "members" && argc == 4) return Members(program, argv[3]);
		if (argc != 6 || !ReadPosition(argv, at)) return Usage();
		if (verb == "resolve") return Resolve(program, argv[3], at);
		if (verb == "type") return Type(program, argv[3], at);
		if (verb == "references") return References(program, argv[3], at);
		return Usage();
	}
}

int main(int argc, char** argv)
{
	if (argc < 3)
		return Usage();
	if (c4a_api_version() < 2)
	{
		std::fprintf(stderr, "c4-query: C4Api.dll implements API %d, this tool needs 2 (compiler %s)\n", c4a_api_version(), c4a_compiler_version());
		return 2;
	}
	c4a_program_options options = {};
	options.root = argv[1];
	c4a_program* program = c4a_program_open(&options);
	int result = 1;
	if (const char* error = c4a_program_error(program))
		std::fprintf(stderr, "c4-query: %s\n", error);
	else
		result = Query(program, argc, argv);
	c4a_program_destroy(program);
	return result;
}
