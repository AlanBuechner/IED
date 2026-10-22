#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include <string>
#include <unordered_set>

namespace Scripting { struct AstNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// One node in a `textDocument/documentSymbol` tree. `kind` uses the numeric LSP SymbolKind
	/// values and `children` holds nested declarations (class members, namespace contents).
	struct DocumentSymbol
	{
		std::string name;          // `ToolRegistry::Add` for an out-of-line definition, as clangd names one
		std::string detail;        // the declaration without its name: `static int64 (const StringView& text) const`
		int kind = 12;             // LSP SymbolKind (Function)
		Protocol::Range range;     // the declaration with its doc comment and annotations
		Protocol::Range selectionRange; // the name token only
		Core::Vector<DocumentSymbol> children;
	};

	class DocumentSymbols
	{
	public:
		/// Builds the symbol tree for one document from its parsed AST, then its macros (`#define` lines, kind
		/// Function, detail "macro"). Only declarations in this file are included; inlined include trees are skipped.
		static Core::Vector<DocumentSymbol> ForDocument(Server::CompilationSession& session, const std::string& uri);

	private:
		struct Walk
		{
			Server::CompilationSession& session;
			uint32 sourceId = 0;
			std::unordered_set<std::string> namespaces; // every namespace path of the program
			std::string scope;                          // the namespace path around the node
		};

		static void CollectChildren(Walk& walk, const Scripting::AstNode& parent, bool members, Core::Vector<DocumentSymbol>& out);
		static void AddDeclaration(Walk& walk, const Scripting::AstNode& node, bool member, Core::Vector<DocumentSymbol>& out);
		static DocumentSymbol MakeSymbol(Walk& walk, const Scripting::AstNode& node, bool member);
		/// Whether a function is a type's, defined out of line: `ToolRegistry::Add` where ToolRegistry names no namespace.
		static bool DefinedForType(const Walk& walk, const Scripting::AstNode& node);
		static void AddMacros(Server::CompilationSession& session, const std::string& uri, Core::Vector<DocumentSymbol>& out);
	};
} // namespace C4LSP::Features
