#include "MacroPreprocessor.h"
#include "Macro/Preprocessor.h"
#include "Macro/MacroExpander.h"
#include "Macro/MacroText.h"

namespace C4Compiler
{
	using namespace MacroText;

	namespace
	{
		bool IsDirectiveLine(std::string_view line)
		{
			const size_t first = SkipSpaces(line, 0);
			return first < line.size() && line[first] == '#';
		}

		// The use's tokens spelled as the expansion spells them (`#define SELF SELF`) is no change: the file's own tokens read the same.
		bool SameTokens(const MacroTokens& a, const MacroTokens& b)
		{
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i].spelling != b[i].spelling) return false;
			return true;
		}
	}

	PreprocessedFile MacroPreprocessor::Process(const std::string& text, const std::string& fileName, MacroTable& table,
		const InheritCallback& inheritMacros)
	{
		return Preprocessor(text, fileName, table, inheritMacros).Run();
	}

	Preprocessor::Preprocessor(const std::string& text, const std::string& fileName, MacroTable& table,
		const MacroPreprocessor::InheritCallback& inheritMacros)
		: m_Text(text), m_FileName(fileName), m_Table(table), m_Inherit(inheritMacros), m_Layout(CreateRef<MacroLayout>())
	{
	}

	PreprocessedFile Preprocessor::Run()
	{
		while (!AtEnd())
		{
			const size_t begin = m_Position;
			const unsigned number = m_Line;
			std::string line = NextPhysicalLine();
			if (!m_InComment && IsDirectiveLine(line))
			{
				Directive(JoinContinuations(std::move(line)), number);
				m_Layout->Skip(static_cast<uint32>(begin), static_cast<uint32>(m_Position));
			}
			else
				CodeLine(line, number, begin);
		}
		for (unsigned open : m_Conditions.OpenLines())
			Report(open, ErrorSeverity::Error, "this #if has no #endif");
		return { std::move(m_Layout), std::move(m_Diagnostics) };
	}

	std::string Preprocessor::PeekPhysicalLine() const
	{
		const size_t eol = m_Text.find('\n', m_Position);
		std::string line = m_Text.substr(m_Position, (eol == std::string::npos ? m_Text.size() : eol) - m_Position);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		return line;
	}

	std::string Preprocessor::NextPhysicalLine()
	{
		std::string line = PeekPhysicalLine();
		const size_t eol = m_Text.find('\n', m_Position);
		m_Position = eol == std::string::npos ? m_Text.size() : eol + 1;
		++m_Line;
		return line;
	}

	// `#define X \` continues on the next line.
	std::string Preprocessor::JoinContinuations(std::string line)
	{
		while (!line.empty() && line.back() == '\\' && !AtEnd())
		{
			line.pop_back();
			line += NextPhysicalLine();
		}
		return line;
	}

	void Preprocessor::CodeLine(const std::string& line, unsigned number, size_t begin)
	{
		size_t codeStart = 0;
		if (m_InComment)
		{
			const size_t close = line.find("*/");
			codeStart = close == std::string::npos ? line.size() : close + 2;
		}
		TrackComments(line);
		if (!m_Conditions.Active())
		{
			m_Layout->Skip(static_cast<uint32>(begin), static_cast<uint32>(m_Position));
			return;
		}
		InheritInclude(line.substr(codeStart));
		ExpandUses(begin + codeStart, number);
	}

	void Preprocessor::ExpandUses(size_t at, unsigned number)
	{
		size_t lineBegin = at;
		while (lineBegin > 0 && m_Text[lineBegin - 1] != '\n') --lineBegin;
		size_t lineEnd = LineEnd(at);
		while (at < lineEnd)
		{
			if (StartsLiteral(m_Text, at)) { at = LiteralEnd(m_Text, at); continue; }
			if (StartsComment(m_Text, at)) { at = CommentEnd(m_Text, at); continue; }
			if (StartsMacroNumber(m_Text, at)) { at = MacroNumberEnd(m_Text, at); continue; }
			if (!IsIdentStart(m_Text[at])) { ++at; continue; }
			const size_t nameEnd = IdentifierEnd(m_Text, at);
			const size_t end = UseEnd(at, nameEnd, lineEnd);
			if (end == 0) { at = nameEnd; continue; }
			Expand(at, end, number, lineBegin);
			at = end;
			if (at > lineEnd) lineEnd = TakeLinesThrough(at);
		}
	}

	size_t Preprocessor::UseEnd(size_t name, size_t nameEnd, size_t lineEnd) const
	{
		const std::string spelled = m_Text.substr(name, nameEnd - name);
		const MacroDefinition* def = m_Table.Get(spelled);
		if (!def) return spelled == "__FILE__" || spelled == "__LINE__" ? nameEnd : 0;
		const size_t open = SkipSpaces(std::string_view(m_Text).substr(0, lineEnd), nameEnd);
		const bool called = open < lineEnd && m_Text[open] == '(';
		if (called && (def->isFunctionLike || NamesFunctionLike(spelled)))
		{
			const size_t end = CallEnd(open);
			if (end != 0) return end;
			return def->isFunctionLike ? 0 : nameEnd; // an unclosed call is left as written
		}
		return def->isFunctionLike ? 0 : nameEnd;
	}

	// `#define CALL F` then `CALL(1)`: the object-like macro expands to the name of a function-like one, which the call calls.
	bool Preprocessor::NamesFunctionLike(const std::string& objectLike) const
	{
		MacroToken name;
		name.kind = MacroToken::Kind::Identifier;
		name.spelling = objectLike;
		const TokenExpansion expanded = MacroExpander::Expand(MacroTokens{ name }, m_Table, { m_FileName, 0 });
		if (expanded.tokens.size() != 1 || !expanded.tokens[0].IsIdentifier()) return false;
		const MacroDefinition* target = m_Table.Get(expanded.tokens[0].spelling);
		return target && target->isFunctionLike;
	}

	size_t Preprocessor::CallEnd(size_t open) const
	{
		int depth = 0;
		for (size_t at = open; at < m_Text.size();)
		{
			if (m_Text[at] == '\n')
			{
				const size_t next = at + 1;
				const size_t eol = m_Text.find('\n', next);
				if (IsDirectiveLine(std::string_view(m_Text).substr(next, (eol == std::string::npos ? m_Text.size() : eol) - next))) return 0;
				++at;
				continue;
			}
			if (StartsLiteral(m_Text, at)) { at = LiteralEnd(m_Text, at); continue; }
			if (StartsComment(m_Text, at)) { at = CommentEnd(m_Text, at); continue; }
			const char c = m_Text[at++];
			if (c == '(') ++depth;
			else if (c == ')' && --depth == 0) return at;
		}
		return 0;
	}

	void Preprocessor::Expand(size_t begin, size_t end, unsigned number, size_t lineBegin)
	{
		const MacroTokens use = ReadMacroTokens(std::string_view(m_Text).substr(begin, end - begin), static_cast<uint32>(begin));
		TokenExpansion expanded = MacroExpander::Expand(use, m_Table, { m_FileName, number });
		for (const std::string& error : expanded.errors)
			Report(number, static_cast<unsigned>(begin - lineBegin + 1), ErrorSeverity::Error, error);
		if (SameTokens(use, expanded.tokens)) return;
		MacroExpansion expansion = MacroExpansion::Of(use.front().spelling, static_cast<uint32>(begin), static_cast<uint32>(end), expanded.tokens);
		if (const MacroDefinition* def = m_Table.Get(expansion.macro))
		{
			expansion.definedIn = def->file;
			expansion.definedLine = def->line;
		}
		m_Layout->Expand(std::move(expansion));
	}

	size_t Preprocessor::TakeLinesThrough(size_t offset)
	{
		while (!AtEnd() && m_Position <= offset)
			TrackComments(NextPhysicalLine());
		return LineEnd(offset);
	}

	size_t Preprocessor::LineEnd(size_t at) const
	{
		const size_t eol = m_Text.find('\n', at);
		size_t end = eol == std::string::npos ? m_Text.size() : eol;
		if (end > at && m_Text[end - 1] == '\r') --end;
		return end;
	}

	// `include "path"`: the included file's macros apply to the lines after this one.
	void Preprocessor::InheritInclude(const std::string& code)
	{
		const size_t start = SkipSpaces(code, 0);
		const size_t end = IdentifierEnd(code, start);
		if (code.compare(start, end - start, "include") != 0) return;
		const size_t open = code.find('"', end);
		const size_t close = open == std::string::npos ? std::string::npos : code.find('"', open + 1);
		if (close != std::string::npos && m_Inherit)
			m_Inherit(code.substr(open + 1, close - open - 1));
	}

	void Preprocessor::TrackComments(const std::string& line)
	{
		size_t i = 0;
		if (m_InComment)
		{
			const size_t close = line.find("*/");
			if (close == std::string::npos) return;
			m_InComment = false;
			i = close + 2;
		}
		while (i < line.size())
		{
			if (StartsLiteral(line, i)) { i = LiteralEnd(line, i); continue; }
			if (line.compare(i, 2, "//") == 0) return;
			if (line.compare(i, 2, "/*") == 0)
			{
				const size_t close = line.find("*/", i + 2);
				if (close == std::string::npos) { m_InComment = true; return; }
				i = close + 2;
				continue;
			}
			++i;
		}
	}

	void Preprocessor::Report(unsigned line, unsigned column, ErrorSeverity severity, const std::string& message)
	{
		m_Diagnostics.push_back({ line, column, severity, message });
	}
}
