#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Server/NodeLocator.h"
#include "Model/SymbolTable.h" // Scripting::SymbolKind for the global-symbol completion list
#include <optional>

namespace Scripting { class AstNode; class Type; }

namespace C4LSP::Server { class CompilationSession; class SemanticResolver; }

namespace C4LSP::Features
{
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
		// Candidates visible at `position`: after a dotted/arrow accessor (`a.` / `a->`) the base
		// object's members (fields, methods, extension and inherited members); after a qualifier
		// (`Render::`, `Render::Pass::`) what it names holds - a namespace's names, a type's members;
		// otherwise enclosing params/locals, innermost class members and all types/globals. Deduped (innermost wins)
		// and filtered to the typed prefix. Resolution drives the shared SemanticResolver, so the
		// base type is exactly what the compiler resolves it to.
		static Core::Vector<CompletionItem> At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);

	private:
		struct Candidate { std::string name; int kind; std::string detail; };

		static bool IsIdentChar(char);
		/// Whether the innermost declaration around the caret is a type's body (not a function or lambda in it).
		static bool InTypeBody(const Core::Vector<const Scripting::AstNode*>& path);
		static bool StartsWith(const std::string& name, const std::string& prefix);

		/// The identifier characters immediately before `position` (the word being completed).
		static std::string WordPrefix(const std::string& text, const Protocol::Position& position);

		/// Byte offset of an LSP position within `text` (same convention as NodeLocator).
		static size_t OffsetAt(const std::string& text, const Protocol::Position& position);

		/// When the cursor is in a member access (`a.` / `a->`, with or without a typed prefix),
		/// return the byte index of the base expression's last character. npos otherwise.
		static size_t FindMemberAccessBaseEnd(const std::string& text, size_t offset);

		/// The identifier immediately before the member-access accessor at `offset` (the base name), or
		/// empty when there is no valid `a.` / `a->` shape under the caret. Mirrors FindMemberAccessBaseEnd.
		static std::string BaseIdentifierAt(const std::string& text, size_t offset);

		/// Resolve the semantic type of the member-access base ending at `baseEndIdx` (pointer /
		/// reference types are dereferenced). Null when it cannot be resolved.
		static const Scripting::Type* ResolveBaseType(Server::CompilationSession& session, uint32 sourceId,
			const Ref<Scripting::AstNode>& root, size_t baseEndIdx);

		/// Fallback for the moment right after `.` / `->` is typed: the member access has not parsed yet
		/// (a bare `a.` is a syntax error), so no AST node exists to resolve. Resolve `baseName` through the
		/// shared SemanticResolver's locals-then-model resolution, which is position-aware and never leaks
		/// another function's same-named local. Null when the name is unknown or has no resolvable type.
		static const Scripting::Type* ResolveBaseTypeByName(const Server::SemanticResolver& resolver,
			const Core::Vector<const Scripting::AstNode*>& lexicalPath, const std::string& baseName);

		/// Add the members visible on `type` (fields + methods for objects/unions, constants for
		/// enums), including inherited object members. Deduped by name; derived wins over base.
		static void CollectMembersOf(const Scripting::Type* type, Core::Vector<Candidate>& out);

		/// Add one object's own fields / methods / extension methods to `out`.
		static void AddMembersOf(const Scripting::ObjectType* obj, Core::Vector<Candidate>& out);

		/// Render a method as `ret(p1, p2)` from its semantic MethodInfo (types only).
		static std::string MethodSignature(const Scripting::ObjectType::MethodInfo& method);

		/// Add parameters of the innermost enclosing function and locals declared in enclosing blocks.
		static void CollectEnclosingContext(const Core::Vector<const Scripting::AstNode*>& path, Core::Vector<Candidate>& out);
		/// Add fields and methods of the innermost enclosing class/struct.
		static void CollectClassMembers(const Core::Vector<const Scripting::AstNode*>& path, Core::Vector<Candidate>& out);
		/// Add every symbol registered in file/namespace/class scopes (globals, functions, types).
		static void CollectGlobalSymbols(const Server::SemanticResolver& resolver, Core::Vector<Candidate>& out);

		/// The names before the word being typed and its `::` (`Render::Pass` for `Render::Pass::Gl|`); empty when the word
		/// is not reached through `::` after names (a leading `::`, a template's arguments).
		static std::string QualifierBefore(const std::string& text, size_t offset);
		/// Add what `qualifier` names holds, as seen from `path`: a namespace's names, or a type's members. False, adding
		/// nothing, when it names neither.
		static bool CollectQualified(const Server::SemanticResolver& resolver, uint32 unitId, const Core::Vector<const Scripting::AstNode*>& path,
			const std::string& qualifier, Core::Vector<Candidate>& out);
		/// Add every name registered in `scope` itself (a namespace's).
		static void AddScopeSymbols(const Scripting::Scope& scope, Core::Vector<Candidate>& out);

		static void AddCandidate(Core::Vector<Candidate>& out, const std::string& name, int kind, const std::string& detail);
		static Core::Vector<CompletionItem> Filter(const Core::Vector<Candidate>& candidates, const std::string& prefix);
		static int SymbolKindToLsp(Scripting::SymbolKind kind);
	};
} // namespace C4LSP::Features
