#pragma once
#include "Core/Core.h"
#include <map>
#include <string>
#include <vector>

namespace Scripting
{
	struct NamespaceNode;

	// What one C4 source declares and what its syntax gets wrong, from the macro preprocessor and the parser alone: no
	// include is read and no name is resolved. The C API's parse (C4Api, docs/tools/04-c4-api.md) is made of it.

	enum class OutlineKind { Namespace, Class, Struct, Interface, Enum, EnumValue, Function, Variable, Union, Alias };
	enum class OutlineSeverity { Warning, Error };

	struct OutlinePosition
	{
		uint32 line = 0;    // 1-based
		uint32 column = 0;  // 1-based, in bytes
	};

	struct OutlineDiagnostic
	{
		OutlineSeverity severity = OutlineSeverity::Error;
		OutlinePosition at;
		std::string message;
	};

	struct OutlineDeclaration
	{
		OutlineKind kind = OutlineKind::Function;
		std::string name;        // as written: `Registry::Add` for a member defined out of its type
		OutlinePosition start;   // its first token
		OutlinePosition end;     // after its last token
		OutlinePosition nameAt;  // its name's first token
		int32 parent = -1;       // the index of the declaration it is in; -1 at file level
	};

	struct SourceOutline
	{
		std::vector<OutlineDiagnostic> diagnostics;   // the preprocessor's, then the lexer's and the parser's, in order
		std::vector<OutlineDeclaration> declarations; // depth first, each before what it holds
	};

	// The outline of `text` with the predefined macros of a default build and `defines` (NAME -> value) on top.
	SourceOutline OutlineOf(const std::string& text, const std::map<std::string, std::string>& defines);

	// The declarations under `root` (a file's parsed namespace), depth first.
	std::vector<OutlineDeclaration> DeclarationsOf(const NamespaceNode& root);
}
