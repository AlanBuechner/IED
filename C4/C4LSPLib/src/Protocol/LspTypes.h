#pragma once
#include "Core/Core.h"
#include "Lexer/Token.h" // Scripting::SourceLocation
#include <string>

namespace C4LSP::Protocol
{
	/// LSP uses 0-based lines and characters; the compiler's SourceLocation is 1-based.
	/// These helpers convert in both directions so feature code can stay in one world.
	struct Position
	{
		uint32 line = 0;      // 0-based
		uint32 character = 0; // 0-based (UTF-16 code units per spec; C4 sources are treated as bytes)

		static Position FromSourceLocation(const Scripting::SourceLocation& loc);
	};

	struct Range
	{
		Position start;
		Position end;

		static Range FromSourceRange(const Scripting::SourceLocation& start, const Scripting::SourceLocation& end);
	};

	/// One text replacement (LSP `TextEdit`): replace [range.start, range.end) with newText.
	struct TextEdit
	{
		Range range;
		std::string newText;
	};

	/// A document identifier: the URI of an open file plus its version.
	struct DocumentUri
	{
		std::string uri; // e.g. "file:///C:/Work/Game/src/Main.cf"

		static std::string ToUri(const std::string& absolutePath);
		static std::string FromUri(const std::string& uri); // absolute path (backslashes on Windows)
	};

	/// A location in a specific file, used by go-to-definition and diagnostics.
	struct Location
	{
		std::string uri;
		Range range;

		static Location Make(const std::string& uri, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end);
	};

	enum class DiagnosticSeverity : int { Error = 1, Warning = 2, Information = 3, Hint = 4 };

	struct Diagnostic
	{
		Range range;
		DiagnosticSeverity severity = DiagnosticSeverity::Error;
		std::string message;
		std::string source; // "c4"
	};

	/// LSP DocumentHighlightKind.
	enum class HighlightKind : int { Text = 1, Read = 2, Write = 3 };

	/// One occurrence of the highlighted symbol in a document.
	struct DocumentHighlight
	{
		Range range;
		HighlightKind kind = HighlightKind::Text;
	};
} // namespace C4LSP::Protocol
