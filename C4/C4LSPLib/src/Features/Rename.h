#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"
#include <set>

namespace Scripting { class AstNode; class ProgramModel; struct SourceLocation; }
namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	// One place the old name should become the new name (editor coordinates, with its file URI).
	struct RenameOccurrence
	{
		std::string uri;
		Protocol::Range range;
	};

	struct RenameResult
	{
		bool found = false;
		Core::Vector<RenameOccurrence> occurrences; // declaration + every reference, deduped
	};

	// The token the rename dialog should select (LSP PrepareRenameResult: range + placeholder).
	struct PrepareResult
	{
		bool found = false;
		Protocol::Range range;
		std::string placeholder;
	};

	/// F2 rename: replaces a symbol's name at its declaration and at every reference across the
	/// whole project. Merged units inline included headers, so the same node is shared by several
	/// unit roots; occurrences are de-duplicated by node pointer.
	class Rename
	{
	public:
		static RenameResult At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);

		// textDocument/prepareRename: the name token of the symbol under the cursor (or null when
		// the position is not on a rename-able declaration or reference). Clients send this before
		// showing the rename input; without it they cannot know which span to select.
		static PrepareResult PrepareAt(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);

	private:
		// The name-token span of a declaration or reference node (not the whole statement).
		struct NameSpan { bool valid = false; Scripting::SourceLocation start, end; };

		static const Scripting::AstNode* DeclarationUnderCursor(const Server::LocatedNode& located, size_t offset);
		static void CollectReferences(const Scripting::AstNode& node, uint32 sourceId, const Server::SemanticResolver& resolver,
			const Core::Vector<const Scripting::AstNode*>& path, const Scripting::AstNode* targetDecl, std::set<const Scripting::AstNode*>& out);
		static NameSpan DeclarationNameSpan(const Scripting::AstNode& decl);
		static NameSpan ReferenceNameSpan(const Scripting::AstNode& node);
		static bool IsReferenceNode(const Scripting::AstNode& node);
		static RenameOccurrence MakeOccurrence(Server::CompilationSession& session, uint32 sourceId, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end);
	};
} // namespace C4LSP::Features
