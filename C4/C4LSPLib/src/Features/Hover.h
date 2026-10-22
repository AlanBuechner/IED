#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"

namespace Scripting { class AstNode; class Type; class ProgramModel; class QualTypeNode; }

namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	/// Hover content for a position: markdown - a `c4` block with the declaration as written, qualified with its owner,
	/// then its doc comment; on a macro use the macro's definition and what the use expands to - plus the hovered
	/// range. `found` is false when no symbol sits under the cursor.
	struct HoverResult
	{
		bool found = false;
		std::string contents; // markdown
		Protocol::Range range;
	};

	class Hover
	{
	public:
		static HoverResult At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
		/// A declaration's markdown: a kind line for a parameter or a local, the `c4` block, then its doc comment.
		static std::string DeclarationMarkdown(Server::CompilationSession& session, const Scripting::AstNode& declaration);

	private:
		static std::string BuildHoverText(Server::CompilationSession& session, const Server::LocatedNode& located);
		static std::string BuildReferenceHover(Server::CompilationSession& session, const Scripting::Type* type, const Scripting::AstNode* declaration);
		static std::string BuildTypeHover(Server::CompilationSession& session, const Scripting::QualTypeNode& type, const Core::Vector<const Scripting::AstNode*>& path);
		/// The declaring node of an object, enum or union type; null for anything else.
		static const Scripting::AstNode* DeclarationOf(const Scripting::Type* type);
		/// A function's declaration in its type (where its modifiers and comment are), else `declaration` itself.
		static const Scripting::AstNode* Documented(Server::CompilationSession& session, const Scripting::AstNode& declaration);
		static std::string MacroHover(const std::string& definition, const std::string& expansion, const std::string& doc);
	};
} // namespace C4LSP::Features
