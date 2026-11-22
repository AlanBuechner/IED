#pragma once
#include "Core/Core.h"
#include "Server/NodeLocator.h"
#include "Lexer/Token.h"
#include <functional>
#include <set>

namespace C4Compiler { class AstNode; struct FunctionDefinitionNode; struct LambdaNode; }
namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	/// Finds every reference to a given declaration. A function's declarations (in a type or a header, and its
	/// out-of-line definition) are one symbol: a reference to any of them counts. Used by find-references,
	/// document-highlight, rename and call hierarchy, which differ only in how they present the hits.
	class ReferenceScanner
	{
	public:
		struct NameRange { C4Compiler::SourceLocation start, end; };

		/// One reference: a name that resolves to the target (an identifier, a member access, a type written in code,
		/// the qualifier `T::` of an out-of-line definition, a lambda's capture), or a construction that calls a target create operator
		/// (`new T(args)`, `T x(args)`, `T(args)`), which spells its type's name rather than the target's.
		struct Occurrence
		{
			const C4Compiler::AstNode* node = nullptr; // the reference, the qualified definition or the construction
			NameRange name;                           // what is named there
			bool spellsTarget = true;                 // false for a construction: a rename leaves it as it is
		};

		/// Whether `node`, written inside `parent`, is a reference (SemanticResolver::IsReference).
		static bool IsReferenceNode(const C4Compiler::AstNode& node, const C4Compiler::AstNode* parent);
		/// The parent of the last node of `path` (the node a reference is written in); null for a path of one.
		static const C4Compiler::AstNode* ParentOfLast(const Core::Vector<const C4Compiler::AstNode*>& path);
		/// The name a reference node names: `Next` of `c.Next` or `Counter::Next`, `Slot` of `Render::Slot<int32>`, the whole of
		/// an identifier.
		static NameRange NameOf(const C4Compiler::AstNode& reference);

		/// Every occurrence of `targetDecl` in `sourceId`'s unit; empty when the target is null.
		static Core::Vector<Occurrence> FindOccurrences(Server::CompilationSession& session, uint32 sourceId, const C4Compiler::AstNode* targetDecl);

		/// The same across every unit of the program, each once: merged units inline headers, so one node is shared by
		/// several unit roots.
		static Core::Vector<Occurrence> FindEverywhere(Server::CompilationSession& session, const C4Compiler::AstNode* targetDecl);

		/// A call of the target (its callee, or a construction calling a create operator) and the function it is made in.
		struct CallSite { NameRange name; const C4Compiler::AstNode* caller = nullptr; };
		/// Every call of `targetDecl` made inside a function, across the program, each once.
		static Core::Vector<CallSite> FindCallSites(Server::CompilationSession& session, const C4Compiler::AstNode* targetDecl);

	private:
		/// Gets each occurrence found with the path to its node (outermost first, the node last).
		using Sink = std::function<void(const Occurrence& occurrence, const Core::Vector<const C4Compiler::AstNode*>& path)>;
		struct Search;
		static std::set<const C4Compiler::AstNode*> TargetsOf(Server::CompilationSession& session, const C4Compiler::AstNode* targetDecl);
		/// Every unit's occurrences of `targets`, each once (merged units share included headers' nodes).
		static void EachUnit(Server::CompilationSession& session, const std::set<const C4Compiler::AstNode*>& targets, const Sink& sink);
		static void Walk(const Search& search, const C4Compiler::AstNode& node, const Core::Vector<const C4Compiler::AstNode*>& path);
		static void Visit(const Search& search, const C4Compiler::AstNode& node, const Core::Vector<const C4Compiler::AstNode*>& path);
		static void VisitQualifiers(const Search& search, const C4Compiler::FunctionDefinitionNode& function, const Core::Vector<const C4Compiler::AstNode*>& path);
		static void VisitCaptures(const Search& search, const C4Compiler::LambdaNode& lambda, const Core::Vector<const C4Compiler::AstNode*>& path);
	};
} // namespace C4LSP::Features
