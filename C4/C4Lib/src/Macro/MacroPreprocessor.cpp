#include "MacroPreprocessor.h"
#include "Macro/Preprocessor.h"
#include "Macro/MacroExpander.h"
#include "Macro/MacroText.h"

namespace Scripting
{
	using namespace MacroText;

	namespace
	{
		bool IsDirectiveLine(const std::string& line)
		{
			const size_t first = SkipSpaces(line, 0);
			return first < line.size() && line[first] == '#';
		}

		// A joined call's text with its comments removed (a `//` would hide the lines joined after it).
		std::string WithoutComments(const std::string& line)
		{
			std::string out;
			for (size_t i = 0; i < line.size();)
			{
				if (StartsLiteral(line, i)) { const size_t end = LiteralEnd(line, i); out.append(line, i, end - i); i = end; continue; }
				if (StartsComment(line, i)) { out += ' '; i = CommentEnd(line, i); continue; }
				out += line[i++];
			}
			return out;
		}
	}

	PreprocessedFile MacroPreprocessor::Process(const std::string& text, const std::string& fileName, MacroTable& table,
		const InheritCallback& inheritMacros)
	{
		return Preprocessor(text, fileName, table, inheritMacros).Run();
	}

	Preprocessor::Preprocessor(const std::string& text, const std::string& fileName, MacroTable& table,
		const MacroPreprocessor::InheritCallback& inheritMacros)
		: m_Text(text), m_FileName(fileName), m_Table(table), m_Inherit(inheritMacros)
	{
		m_Out.reserve(text.size());
	}

	PreprocessedFile Preprocessor::Run()
	{
		while (!AtEnd())
		{
			const unsigned number = m_Line;
			std::string line = NextPhysicalLine();
			if (!m_InComment && IsDirectiveLine(line))
				Directive(JoinContinuations(std::move(line)), number);
			else
				CodeLine(line, number);
		}
		for (unsigned open : m_Conditions.OpenLines())
			Report(open, ErrorSeverity::Error, "this #if has no #endif");
		return { std::move(m_Out), std::move(m_Diagnostics) };
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

	// `#define X \` continues on the next line; each joined line still becomes one (empty) output line.
	std::string Preprocessor::JoinContinuations(std::string line)
	{
		while (!line.empty() && line.back() == '\\' && !AtEnd())
		{
			line.pop_back();
			line += NextPhysicalLine();
			m_Out += '\n';
		}
		return line;
	}

	void Preprocessor::CodeLine(const std::string& line, unsigned number)
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
			m_Out += '\n';
			return;
		}
		std::string code = line.substr(codeStart);
		size_t lines = 1;
		if (!m_InComment && MacroExpander::EndsInsideCall(code, m_Table))
			lines += JoinOpenCall(code);
		InheritInclude(code);
		const ExpansionResult expanded = MacroExpander::Expand(code, m_Table, { m_FileName, number });
		for (const std::string& error : expanded.errors)
			Report(number, ErrorSeverity::Error, error);
		m_Out += line.substr(0, codeStart);
		m_Out += expanded.text;
		m_Out.append(lines, '\n');
	}

	// A macro call that continues on later lines is read whole; the lines it took stay empty in the output.
	size_t Preprocessor::JoinOpenCall(std::string& code)
	{
		size_t joined = 0;
		code = WithoutComments(code);
		while (!AtEnd() && !m_InComment && MacroExpander::EndsInsideCall(code, m_Table) && !IsDirectiveLine(PeekPhysicalLine()))
		{
			const std::string next = NextPhysicalLine();
			TrackComments(next);
			code += ' ' + WithoutComments(next);
			++joined;
		}
		return joined;
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

	void Preprocessor::Report(unsigned line, ErrorSeverity severity, const std::string& message)
	{
		m_Diagnostics.push_back({ line, 1, severity, message });
	}
}
