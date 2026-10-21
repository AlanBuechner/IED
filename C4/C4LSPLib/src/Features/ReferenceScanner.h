#pragma once
#include "Core/Core.h"
#include "Server/NodeLocator.h"
#include "Lexer/Token.h"
#include <set>

namespace Scripting { class AstNode; }
namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	/// Finds every reference to a given declaration. A function's declarations (in a type or a header, and its
	/// out-of-line definition) are one symbol: a reference to any of them counts. Used by find-references,
	/// document-highlight and rename, which differ only in how they present the hits.
	class ReferenceScanner
	{
	public:
		struct NameRange { Scripting::SourceLocation start, end; };

		static bool IsReferenceNode(const Scripting::AstNode& node);
		/// The name a reference node names: `Next` of `c.Next` or `Counter::Next`, the whole of an identifier.
		static NameRange NameOf(const Scripting::AstNode& reference);

		/// All reference nodes (identifier / member-access) in `sourceId`'s unit that resolve to `targetDecl`.
		/// Empty when the target is null or has no references.
		static Core::Vector<const Scripting::AstNode*> FindOccurrences(Server::CompilationSession& session, uint32 sourceId, const Scripting::AstNode* targetDecl);

		/// The same across every unit of the program, each node once: merged units inline headers, so one
		/// reference node is shared by several unit roots.
		static Core::Vector<const Scripting::AstNode*> FindEverywhere(Server::CompilationSession& session, const Scripting::AstNode* targetDecl);

	private:
		static std::set<const Scripting::AstNode*> TargetsOf(Server::CompilationSession& session, const Scripting::AstNode* targetDecl);
		static void Walk(const Scripting::AstNode& node, uint32 sourceId, const Server::SemanticResolver& resolver,
			const Core::Vector<const Scripting::AstNode*>& path, const std::set<const Scripting::AstNode*>& targets, Core::Vector<const Scripting::AstNode*>& out);
	};
} // namespace C4LSP::Features
