#include "Features/Diagnostics.h"
#include "Server/CompilationSession.h"
#include "Compiler/ErrorTracker.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

namespace C4LSP::Features
{
	// Semantic errors carry only a start location, so the raw conversion is a zero-width range. Monaco
	// renders zero-width markers as invisible (no squiggle), which hides real type/semantic problems in
	// the editor even though they reach the client. Widen such ranges to the full token at that position:
	// an identifier run, or a whole string literal when the point lands on or inside it. Parse errors
	// already carry real spans and pass through untouched (their range is non-zero-width).
	static void WidenToTokenSpan(Server::CompilationSession& session, const std::string& uri, Protocol::Diagnostic& diag)
	{
		if (diag.range.start.line != diag.range.end.line || diag.range.start.character != diag.range.end.character)
			return;

		std::string text = session.GetEditorText(uri);
		size_t lineIndex = 0; // index of the first character on diag.range.start.line (0-based)
		for (uint32 i = 0; i < diag.range.start.line; ++i) {
			size_t nl = text.find('\n', lineIndex); // newline ending this line
			if (nl == std::string::npos) break;      // past the last line; clamp to end
			lineIndex = nl + 1;                      // first character of the next line
		}

		size_t pos = lineIndex + diag.range.start.character;
		if (pos >= text.size())
			return; // position is out of range for this text; keep the point as-is

		auto isIdentChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };

		// Scan this line from its start to `pos` with a tiny string-state machine: after the scan,
		// inString tells whether the position sits INSIDE a literal (the compiler reports call-argument
		// errors at an arbitrary character of the argument expression, which lands mid-string for
		// literal arguments - e.g. inside "not a number"), and openQuote is that literal's opening quote.
		bool inString = false, escaped = false;
		size_t openQuote = lineIndex;
		for (size_t i = lineIndex; i < pos && i < text.size(); ++i)
		{
			char c = text[i];
			if (!inString) { if (c == '"') { inString = true; openQuote = i; } continue; }
			if (escaped) escaped = false;
			else if (c == '\\') escaped = true;
			else if (c == '"') inString = false;
		}

		size_t end = pos;
		if (inString || text[pos] == '"') {
			// Widen to the whole literal: from its opening quote past its unescaped closing quote.
			diag.range.start.character = static_cast<uint32_t>((inString ? openQuote : pos) - lineIndex);
			end = (inString ? openQuote : pos) + 1;
			bool esc = false;
			while (end < text.size() && text[end] != '\n') {
				if (esc) esc = false;
				else if (text[end] == '\\') esc = true;
				else if (text[end] == '"') { ++end; break; } // include the closing quote
				++end;
			}
			diag.range.end.character = static_cast<uint32_t>(std::min(end, text.size()) - lineIndex);
			return;
		}

		if (isIdentChar(text[pos])) {
			// Identifier/number token: the error may land on any character of it, so extend in BOTH
			// directions over [a-zA-Z0-9_] to cover the whole name.
			size_t start = pos;
			while (start > lineIndex && isIdentChar(text[start - 1])) --start;
			diag.range.start.character = static_cast<uint32_t>(start - lineIndex);
			end = pos;
			while (end < text.size() && text[end] != '\n' && isIdentChar(text[end])) ++end;
		}

		if (diag.range.start.line != diag.range.end.line)
			return; // pathological: token crossed a line boundary, keep the point range
		diag.range.end.character = static_cast<uint32_t>(std::min(end, text.size()) - lineIndex);
	}

	Core::Vector<Protocol::Diagnostic> Diagnostics::ForDocument(Server::CompilationSession& session, const std::string& uri)
	{
		Core::Vector<Protocol::Diagnostic> result;
		// A session that could analyze nothing (a script interface that does not read) says why on every document.
		if (!session.GetSetupProblem().empty())
		{
			Protocol::Diagnostic problem;
			problem.message = session.GetSetupProblem();
			problem.source = "c4";
			result.Push(problem);
			return result;
		}
		uint32 targetId = session.FindSourceIdByUri(uri);
		if (targetId == 0)
			return result;

		// The include-merge can surface the same error under more than one unit, so dedupe by
		// (line, column, message) to avoid showing every squiggle twice.
		std::set<std::tuple<uint32, uint32, std::string>> seen;

		// The compiler's complete diagnostic surface for this file: parse + program-model errors
		// (GetAllDiagnostics) plus body-level HIR-lowering type errors (GetLoweringDiagnostics). Both are
		// computed by the compiler itself - the LSP adds no error logic of its own - so what the editor
		// shows is exactly what C4.exe would report for this project.
		auto push = [&](const Scripting::CompilationError& error) {
			if (error.location.sourceId != targetId)
				return;
			Protocol::Diagnostic diag = ToLsp(session, error, targetId);
			WidenToTokenSpan(session, uri, diag);
			uint32 line = diag.range.start.line;
			uint32 col = diag.range.start.character;
			if (!seen.insert(std::make_tuple(line, col, diag.message)).second)
				return; // duplicate of an already-reported diagnostic
			result.Push(diag);
		};

		for (const auto& error : session.GetCompiler().GetAllDiagnostics())
			push(error);
		for (const auto& error : session.GetLoweringDiagnostics())
			push(error);

		return result;
	}

	Protocol::Diagnostic Diagnostics::ToLsp(Server::CompilationSession& session, const Scripting::CompilationError& error, uint32 sourceId)
	{
		Protocol::Diagnostic diag;
		diag.range = session.ToEditorRange(sourceId, error.location, error.location); // point range
		diag.severity = SeverityToLsp(error.severity);
		diag.message = error.message;
		diag.source = "c4";
		return diag;
	}

	Protocol::DiagnosticSeverity Diagnostics::SeverityToLsp(Scripting::ErrorSeverity severity)
	{
		switch (severity)
		{
		case Scripting::ErrorSeverity::Warning: return Protocol::DiagnosticSeverity::Warning;
		case Scripting::ErrorSeverity::Fatal:
		case Scripting::ErrorSeverity::Error:   return Protocol::DiagnosticSeverity::Error;
		default:                                 return Protocol::DiagnosticSeverity::Information;
		}
	}
} // namespace C4LSP::Features
