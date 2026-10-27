// The C4 compiler's C API over C4Lib (docs/tools/06-c4-api.md).

#define IMPLEMENT_LOGGER // the DLL's own copy of Core's loggers, which C4Lib logs through
#include "Core/Logging/Log.h"
#include "C4Api.h"
#include "Compiler/Version.h"
#include "Macro/PredefinedMacros.h"
#include "Outline/SourceOutline.h"
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

struct c4a_parse
{
	bool read = true;
	Scripting::SourceOutline outline;
};

namespace
{
	const char* kKindNames[] = { "namespace", "class", "struct", "interface", "enum", "enum value", "function", "variable", "union", "alias" };
	constexpr int32_t kKinds = static_cast<int32_t>(sizeof(kKindNames) / sizeof(kKindNames[0]));

	std::string VersionText()
	{
		std::ostringstream text;
		text << Scripting::CompilerVersion::Major << "." << Scripting::CompilerVersion::Minor << "." << Scripting::CompilerVersion::Patch;
		return text.str();
	}

	std::map<std::string, std::string> DefinesOf(const c4a_parse_options& options)
	{
		std::map<std::string, std::string> defines;
		for (uint32_t i = 0; i < options.define_count; ++i)
			if (options.defines && options.defines[i])
			{
				const Scripting::MacroDefine define = Scripting::ParseMacroDefine(options.defines[i]);
				defines[define.name] = define.value;
			}
		return defines;
	}

	bool ReadFile(const char* path, std::string& text)
	{
		std::ifstream file(path ? path : "", std::ios::binary);
		if (!file) return false;
		text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	c4a_position PositionOf(const Scripting::OutlinePosition& at) { return { at.line, at.column }; }

	c4a_kind KindOf(Scripting::OutlineKind kind)
	{
		using Scripting::OutlineKind;
		switch (kind)
		{
		case OutlineKind::Namespace: return C4A_NAMESPACE;
		case OutlineKind::Class: return C4A_CLASS;
		case OutlineKind::Struct: return C4A_STRUCT;
		case OutlineKind::Interface: return C4A_INTERFACE;
		case OutlineKind::Enum: return C4A_ENUM;
		case OutlineKind::EnumValue: return C4A_ENUM_VALUE;
		case OutlineKind::Function: return C4A_FUNCTION;
		case OutlineKind::Variable: return C4A_VARIABLE;
		case OutlineKind::Union: return C4A_UNION;
		case OutlineKind::Alias: return C4A_ALIAS;
		}
		return C4A_VARIABLE;
	}

	c4a_parse* Unread(const char* path)
	{
		c4a_parse* parse = new c4a_parse();
		parse->read = false;
		parse->outline.diagnostics.push_back({ Scripting::OutlineSeverity::Error, {}, std::string("cannot read ") + (path ? path : "(no path)") });
		return parse;
	}
}

extern "C"
{
	int32_t c4a_api_version(void) { return C4A_API_VERSION; }

	const char* c4a_compiler_version(void)
	{
		static const std::string version = VersionText();
		return version.c_str();
	}

	c4a_parse* c4a_parse_source(const c4a_parse_options* options)
	{
		if (!options) return Unread(nullptr);
		std::string text;
		if (options->text) text.assign(options->text, static_cast<size_t>(options->length));
		else if (!ReadFile(options->path, text)) return Unread(options->path);
		c4a_parse* parse = new c4a_parse();
		parse->outline = Scripting::OutlineOf(text, DefinesOf(*options));
		return parse;
	}

	int32_t c4a_parse_read(const c4a_parse* parse) { return parse && parse->read ? 1 : 0; }

	uint32_t c4a_diagnostic_count(const c4a_parse* parse) { return parse ? static_cast<uint32_t>(parse->outline.diagnostics.size()) : 0; }

	int32_t c4a_diagnostic_at(const c4a_parse* parse, uint32_t index, c4a_diagnostic* out)
	{
		if (!parse || !out || index >= parse->outline.diagnostics.size()) return 0;
		const Scripting::OutlineDiagnostic& found = parse->outline.diagnostics[index];
		*out = { found.severity == Scripting::OutlineSeverity::Warning ? C4A_WARNING : C4A_ERROR, PositionOf(found.at), found.message.c_str() };
		return 1;
	}

	uint32_t c4a_declaration_count(const c4a_parse* parse) { return parse ? static_cast<uint32_t>(parse->outline.declarations.size()) : 0; }

	int32_t c4a_declaration_at(const c4a_parse* parse, uint32_t index, c4a_declaration* out)
	{
		if (!parse || !out || index >= parse->outline.declarations.size()) return 0;
		const Scripting::OutlineDeclaration& found = parse->outline.declarations[index];
		*out = { KindOf(found.kind), found.name.c_str(), PositionOf(found.start), PositionOf(found.end), PositionOf(found.nameAt), found.parent };
		return 1;
	}

	const char* c4a_kind_name(c4a_kind kind)
	{
		const int32_t at = static_cast<int32_t>(kind);
		return at >= 0 && at < kKinds ? kKindNames[at] : "";
	}

	void c4a_parse_destroy(c4a_parse* parse) { delete parse; }
}
