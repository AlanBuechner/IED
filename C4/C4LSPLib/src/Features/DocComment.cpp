#include "Features/DocComment.h"
#include "Features/DeclarationText.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include <algorithm>

namespace C4LSP::Features
{
	using Scripting::NodeType;

	static bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

	static std::string_view Trimmed(std::string_view text)
	{
		while (!text.empty() && IsBlank(text.front()))
			text.remove_prefix(1);
		while (!text.empty() && IsBlank(text.back()))
			text.remove_suffix(1);
		return text;
	}

	static size_t LineStartOf(std::string_view text, size_t at)
	{
		return at == 0 ? 0 : text.rfind('\n', at - 1) + 1;
	}

	std::string DocComment::Cleaned(std::string_view comment)
	{
		std::string out;
		std::string_view rest = comment;
		while (!rest.empty())
		{
			const size_t newline = rest.find('\n');
			std::string_view line = Trimmed(rest.substr(0, newline));
			rest = newline == std::string_view::npos ? std::string_view() : rest.substr(newline + 1);
			for (const char* marker : { "///", "//", "/**", "/*" })
				if (line.substr(0, std::char_traits<char>::length(marker)) == marker)
				{
					line.remove_prefix(std::char_traits<char>::length(marker));
					break;
				}
			if (line.size() >= 2 && line.substr(line.size() - 2) == "*/")
				line.remove_suffix(2);
			if (!line.empty() && line.front() == '*')
				line.remove_prefix(1);
			line = Trimmed(line);
			if (!line.empty() || !out.empty())
				out += (out.empty() ? "" : "\n") + std::string(line);
		}
		while (!out.empty() && out.back() == '\n')
			out.pop_back();
		return out;
	}

	std::string DocComment::Above(std::string_view text, size_t lineStart, size_t& commentStart, uint32& lines)
	{
		commentStart = lineStart;
		lines = 0;
		while (commentStart > 0)
		{
			const size_t previous = LineStartOf(text, commentStart - 1);
			std::string_view line = Trimmed(text.substr(previous, commentStart - 1 - previous));
			if (line.substr(0, 2) == "//")
			{
				commentStart = previous;
				++lines;
				continue;
			}
			if (line.size() >= 2 && line.substr(line.size() - 2) == "*/")
			{
				const size_t open = text.rfind("/*", commentStart - 1);
				if (open == std::string_view::npos || !Trimmed(text.substr(LineStartOf(text, open), open - LineStartOf(text, open))).empty())
					break;
				const size_t blockStart = LineStartOf(text, open);
				lines += static_cast<uint32>(std::count(text.begin() + blockStart, text.begin() + commentStart, '\n'));
				commentStart = blockStart;
			}
			break;
		}
		return Cleaned(text.substr(commentStart, lineStart - commentStart));
	}

	std::string DocComment::After(std::string_view text, size_t from)
	{
		const size_t lineEnd = std::min(text.find('\n', from), text.size());
		const size_t comment = text.substr(0, lineEnd).find("//", from);
		if (comment == std::string_view::npos)
			return "";
		return Cleaned(text.substr(comment, lineEnd - comment));
	}

	// Where a field, constant, enum value or alias ends; npos for a declaration that is not one.
	static size_t TrailingFrom(const Scripting::AstNode& decl)
	{
		switch (decl.Type())
		{
		case NodeType::VariableDefinition: {
			const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(decl);
			return var.GetSemicolonToken().GetStartLocation().line != 0 ? var.GetSemicolonToken().GetEndLocation().byteOffset : decl.endLocation.byteOffset;
		}
		case NodeType::EnumMember:
		case NodeType::AliasDefinition:
			return decl.endLocation.byteOffset;
		default:
			return std::string_view::npos;
		}
	}

	std::string DocComment::Of(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		std::string_view text = DeclarationText::SourceText(session, decl.startLocation.sourceId);
		if (text.empty() || decl.startLocation.byteOffset > text.size())
			return "";
		size_t commentStart = 0;
		uint32 lines = 0;
		std::string above = Above(text, LineStartOf(text, decl.startLocation.byteOffset), commentStart, lines);
		if (!above.empty())
			return above;
		const size_t end = TrailingFrom(decl);
		return end <= text.size() ? After(text, end) : std::string();
	}

	Scripting::SourceLocation DocComment::Start(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		std::string_view text = DeclarationText::SourceText(session, decl.startLocation.sourceId);
		if (text.empty() || decl.startLocation.byteOffset > text.size())
			return decl.startLocation;
		size_t commentStart = 0;
		uint32 lines = 0;
		Above(text, LineStartOf(text, decl.startLocation.byteOffset), commentStart, lines);
		if (lines == 0 || lines >= decl.startLocation.line)
			return decl.startLocation;
		Scripting::SourceLocation start = decl.startLocation;
		start.line -= lines;
		start.column = 1;
		start.byteOffset = commentStart;
		return start;
	}
} // namespace C4LSP::Features
