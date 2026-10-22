#pragma once
#include "Core/Core.h"
#include "Lexer/Token.h"
#include <string>
#include <string_view>

namespace Scripting { struct AstNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// A declaration's doc comment: the comment lines right above it (`//`, `///` or one `/* */` block, with no blank
	/// line between), without their markers; else the comment after a field, constant, enum value or alias on its line.
	class DocComment
	{
	public:
		static std::string Of(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// Where the comment above the declaration begins; its own start when it has none.
		static Scripting::SourceLocation Start(Server::CompilationSession& session, const Scripting::AstNode& decl);
		/// The comment lines above the line that begins at `lineStart`, markers removed; `commentStart` is the offset of
		/// the first of them (`lineStart` when there are none) and `lines` how many there are.
		static std::string Above(std::string_view text, size_t lineStart, size_t& commentStart, uint32& lines);
		/// The `//` comment after `from` on its line, markers removed; empty when there is none.
		static std::string After(std::string_view text, size_t from);

	private:
		static std::string Cleaned(std::string_view comment);
	};
} // namespace C4LSP::Features
