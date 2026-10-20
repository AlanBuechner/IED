#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"

namespace Scripting { class AstNode; class Type; class ProgramModel; }

namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	/// Hover content for a position: markdown text (resolved type or signature) plus the
	/// hovered token's range. `found` is false when no symbol sits under the cursor.
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

	private:
		static std::string BuildHoverText(const Server::SemanticResolver& resolver, const Server::LocatedNode& located);
		static std::string BuildReferenceHover(const Scripting::AstNode& node, const Scripting::Type* type, const Scripting::AstNode* declaration);
		static std::string BuildDeclarationHover(const Scripting::AstNode& node);
	};
} // namespace C4LSP::Features
