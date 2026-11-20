#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"
#include "Model/SymbolTable.h" // Scripting::SymbolKind for the global-symbol completion list
#include <optional>

namespace Scripting { class Type; }

namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
	class CompletionCandidates;

	/// One LSP completion item. `kind` uses the numeric LSP CompletionItemKind values so it can be
	/// emitted directly into the response without further mapping.
	struct CompletionItem
	{
		std::string label;   // symbol name (also used as filterText)
		int kind = 6;        // LSP CompletionItemKind
		std::string detail;  // short type / signature shown beside the item
		// An item that replaces a range of its own (the part of an include's path being typed): the range, and its text.
		std::optional<Protocol::Range> edit;
		std::string newText;
		std::string sortText;       // where the client lists it; empty: by its label
		bool completeAgain = false; // the client asks for completion again once it is in (a folder of an include's path)
	};

	class Completion
	{
	public:
		// Candidates visible at `position`: after an accessor (`a.`, `a->`, `GetTransform()->`, `m_Body.Shape.`) the members of
		// what it is made on (fields, methods, extension and inherited members); after a qualifier (`Render::`,
		// `Render::Pass::`) what it names holds - a namespace's names, a type's members; otherwise enclosing params/locals,
		// the members an implicit `this` reaches, and all types/globals. Each name once (innermost wins), filtered to the
		// typed prefix. Resolution drives the shared SemanticResolver, so a type is exactly what the compiler resolves.
		static Core::Vector<CompletionItem> At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);

		// The same at `position` of `text`, the editor's text now, which `session` may have analyzed before some edits: the
		// words around the caret are read from `text`, and the scopes around it from the analysis, at the place the edits
		// since leave it (Server::PlaceInAnalyzedText) - so a completion never waits for an analysis of each keystroke.
		static Core::Vector<CompletionItem> At(Server::CompilationSession& session, const std::string& uri, const std::string& text, const Protocol::Position& position);

	private:
		static bool IsIdentChar(char);
		/// Whether the innermost declaration around the caret is a type's body (not a function or lambda in it).
		static bool InTypeBody(const Core::Vector<const Scripting::AstNode*>& path);

		/// The identifier characters immediately before byte `offset` (the word being completed).
		static std::string WordBefore(const std::string& text, size_t offset);

		/// Byte offset of an LSP position within `text` (same convention as NodeLocator).
		static size_t OffsetAt(const std::string& text, const Protocol::Position& position);

		/// Add the members visible on `type` (fields + methods for objects/unions, constants for enums), including what
		/// every base declares; a pointer's are its pointee's. The most derived wins a name.
		static void CollectMembersOf(const Scripting::Type* type, CompletionCandidates& out);

		/// Add one object's own fields / methods / extension methods to `out`.
		static void AddMembersOf(const Scripting::ObjectType* obj, CompletionCandidates& out);

		/// Render a method as `ret(p1, p2)` from its semantic MethodInfo (types only).
		static std::string MethodSignature(const Scripting::ObjectType::MethodInfo& method);

		/// Add parameters of the innermost enclosing function and locals declared in enclosing blocks.
		static void CollectEnclosingContext(const Core::Vector<const Scripting::AstNode*>& path, CompletionCandidates& out);
		/// Add fields and methods of the innermost enclosing class/struct.
		static void CollectClassMembers(const Core::Vector<const Scripting::AstNode*>& path, CompletionCandidates& out);
		/// Add every symbol registered in file/namespace scopes (globals, functions, types).
		static void CollectGlobalSymbols(const Server::SemanticResolver& resolver, CompletionCandidates& out);

		/// The names before the word being typed and its `::` (`Render::Pass` for `Render::Pass::Gl|`); empty when the word
		/// is not reached through `::` after names (a leading `::`, a template's arguments).
		static std::string QualifierBefore(const std::string& text, size_t offset);
		/// Add what `qualifier` names holds, as seen from `path`: a namespace's names, or a type's members. False, adding
		/// nothing, when it names neither.
		static bool CollectQualified(const Server::SemanticResolver& resolver, uint32 unitId, const Core::Vector<const Scripting::AstNode*>& path,
			const std::string& qualifier, CompletionCandidates& out);
		/// Add every name registered in `scope` itself (a namespace's).
		static void AddScopeSymbols(const Scripting::Scope& scope, CompletionCandidates& out);

		static int SymbolKindToLsp(Scripting::SymbolKind kind);
	};
} // namespace C4LSP::Features
