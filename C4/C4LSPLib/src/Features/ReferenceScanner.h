#pragma once
#include "Core/Core.h"
#include "Server/NodeLocator.h"

namespace Scripting { class AstNode; }
namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	/// Finds every reference to a given declaration within one document's AST. Used by both
	/// find-references and document-highlight, which differ only in how they present the hits.
	class ReferenceScanner
	{
	public:
		/// All reference nodes (identifier / member-access) in `sourceId`'s unit that resolve to
		/// exactly `targetDecl`. Empty when the target is null or has no references.
		static Core::Vector<const Scripting::AstNode*> FindOccurrences(Server::CompilationSession& session, uint32 sourceId, const Scripting::AstNode* targetDecl);

	private:
		static bool IsReferenceNode(const Scripting::AstNode& node);
		static void Walk(const Scripting::AstNode& node, uint32 sourceId, const Server::SemanticResolver& resolver,
			const Core::Vector<const Scripting::AstNode*>& path, const Scripting::AstNode* targetDecl, Core::Vector<const Scripting::AstNode*>& out);
	};
} // namespace C4LSP::Features
