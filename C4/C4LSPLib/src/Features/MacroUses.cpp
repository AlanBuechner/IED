#include "Features/MacroUses.h"
#include "Features/DocComment.h"
#include "Server/CompilationSession.h"
#include "Macro/MacroExpander.h"
#include "Macro/MacroTable.h"
#include <algorithm>

namespace C4LSP::Features
{
	static bool IsNameChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

	static bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

	// A 0-based line and byte column of `text` as a byte offset; npos past its end.
	static size_t OffsetOf(std::string_view text, const Protocol::Position& position)
	{
		size_t at = 0;
		for (uint32 line = 0; line < position.line; ++line)
		{
			at = text.find('\n', at);
			if (at == std::string_view::npos)
				return std::string_view::npos;
			++at;
		}
		const size_t lineEnd = std::min(text.find('\n', at), text.size());
		return std::min(at + position.character, lineEnd);
	}

	static Protocol::Position PositionOf(std::string_view text, size_t offset)
	{
		Protocol::Position position;
		size_t lineStart = 0;
		for (size_t i = 0; i < offset && i < text.size(); ++i)
			if (text[i] == '\n')
			{
				++position.line;
				lineStart = i + 1;
			}
		position.character = static_cast<uint32>(offset - lineStart);
		return position;
	}

	// A line ending in a backslash (before any carriage return) continues on the next one.
	static bool EndsWithBackslash(std::string_view line)
	{
		while (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		return !line.empty() && line.back() == '\\';
	}

	// A directive's lines as one: each backslash that continues a line, and the line break after it, become a space.
	static std::string Joined(std::string_view directive)
	{
		std::string out;
		for (size_t at = 0; at < directive.size(); ++at)
		{
			size_t next = at + 1;
			while (next < directive.size() && directive[next] == '\r')
				++next;
			if (directive[at] == '\\' && next < directive.size() && directive[next] == '\n')
			{
				out += ' ';
				at = next;
			}
			else if (directive[at] != '\r')
				out += directive[at];
		}
		return out;
	}

	// The `#` of a directive line: the first character that is not a blank. npos for any other line.
	static size_t DirectiveAt(std::string_view text, size_t lineStart)
	{
		size_t at = lineStart;
		while (at < text.size() && IsBlank(text[at]))
			++at;
		return at < text.size() && text[at] == '#' ? at : std::string_view::npos;
	}

	static size_t SkipBlanks(std::string_view text, size_t at, size_t end)
	{
		while (at < end && IsBlank(text[at]))
			++at;
		return at;
	}

	// The end of the line at `lineStart`, and of the lines a backslash continues it on.
	static size_t DirectiveEnd(std::string_view text, size_t lineStart)
	{
		size_t end = std::min(text.find('\n', lineStart), text.size());
		while (end < text.size() && EndsWithBackslash(text.substr(lineStart, end - lineStart)))
			end = std::min(text.find('\n', end + 1), text.size());
		return end;
	}

	// The `#define` that begins at `lineStart` and ends at `end`; false for any other line.
	static bool ReadDefinition(std::string_view text, size_t lineStart, size_t end, MacroUses::Definition& definition)
	{
		const size_t hash = DirectiveAt(text, lineStart);
		if (hash == std::string_view::npos || hash >= end)
			return false;
		const size_t keyword = SkipBlanks(text, hash + 1, end);
		if (text.substr(keyword, 6) != "define" || keyword + 6 >= end || !IsBlank(text[keyword + 6]))
			return false;
		const size_t nameStart = SkipBlanks(text, keyword + 6, end);
		size_t nameEnd = nameStart;
		while (nameEnd < end && IsNameChar(text[nameEnd]))
			++nameEnd;
		if (nameEnd == nameStart)
			return false;
		definition.name = std::string(text.substr(nameStart, nameEnd - nameStart));
		definition.text = Joined(text.substr(hash, end - hash));
		size_t commentStart = 0;
		uint32 lines = 0;
		definition.doc = DocComment::Above(text, lineStart, commentStart, lines);
		if (definition.doc.empty())
			definition.doc = DocComment::After(text, nameEnd);
		definition.range = { PositionOf(text, commentStart), PositionOf(text, end) };
		definition.nameRange = { PositionOf(text, nameStart), PositionOf(text, nameEnd) };
		return true;
	}

	Core::Vector<MacroUses::Definition> MacroUses::DefinitionsIn(std::string_view text)
	{
		Core::Vector<Definition> out;
		for (size_t lineStart = 0; lineStart < text.size();)
		{
			const size_t end = DirectiveEnd(text, lineStart);
			Definition definition;
			if (ReadDefinition(text, lineStart, end, definition))
				out.Push(definition);
			lineStart = end + 1;
		}
		return out;
	}

	std::string MacroUses::Rendered(const Scripting::MacroDefinition& macro)
	{
		std::string text = "#define " + macro.name;
		if (macro.isFunctionLike)
		{
			text += "(";
			for (size_t i = 0; i < macro.params.size(); ++i)
				text += (i > 0 ? ", " : "") + macro.params[i];
			text += ")";
		}
		return macro.body.empty() ? text : text + " " + macro.body;
	}

	size_t MacroUses::CallEnd(std::string_view text, size_t at)
	{
		while (at < text.size() && (IsBlank(text[at]) || text[at] == '\n'))
			++at;
		if (at >= text.size() || text[at] != '(')
			return std::string_view::npos;
		int depth = 0;
		for (; at < text.size(); ++at)
		{
			const char c = text[at];
			if (c == '"' || c == '\'')
			{
				for (++at; at < text.size() && text[at] != c; ++at)
					if (text[at] == '\\')
						++at;
			}
			else if (c == '(')
				++depth;
			else if (c == ')' && --depth == 0)
				return at + 1;
		}
		return std::string_view::npos;
	}

	MacroUses::Use MacroUses::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		Use use;
		const uint32 sourceId = session.FindSourceIdByUri(uri);
		const Scripting::MacroTable* table = session.MacroTableOf(sourceId);
		const std::string text = session.GetEditorText(uri);
		const size_t at = OffsetOf(text, position);
		if (!table || at == std::string_view::npos)
			return use;

		size_t start = at;
		while (start > 0 && IsNameChar(text[start - 1]))
			--start;
		size_t end = at;
		while (end < text.size() && IsNameChar(text[end]))
			++end;
		const size_t lineStart = start == 0 ? 0 : text.rfind('\n', start - 1) + 1;
		const Scripting::MacroDefinition* macro = end > start ? table->Get(text.substr(start, end - start)) : nullptr;
		if (!macro || DirectiveAt(text, lineStart) != std::string_view::npos)
			return use;

		size_t useEnd = end;
		if (macro->isFunctionLike)
		{
			const size_t callEnd = CallEnd(text, end);
			if (callEnd == std::string_view::npos)
				return use; // a function-like macro's name without a call is not expanded
			useEnd = callEnd;
		}
		Scripting::ExpansionSite site;
		site.file = session.GetSourceManager().HasInfo(sourceId) ? session.GetSourceManager().GetInfo(sourceId).displayName : uri;
		site.line = position.line + 1;
		use.found = true;
		use.name = macro->name;
		use.definition = Rendered(*macro);
		use.expansion = Scripting::MacroExpander::Expand(text.substr(start, useEnd - start), *table, site).text;
		use.range = { PositionOf(text, start), PositionOf(text, useEnd) };
		for (const Definition& definition : DefinitionsIn(text))
			if (definition.name == macro->name)
				use.doc = definition.doc;
		return use;
	}
} // namespace C4LSP::Features
