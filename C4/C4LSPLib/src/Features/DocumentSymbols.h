#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"

namespace Scripting { struct AstNode; struct FunctionDefinitionNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// One node in a `textDocument/documentSymbol` tree. `kind` uses the numeric LSP SymbolKind
	/// values and `children` holds nested declarations (class members, namespace contents).
	struct DocumentSymbol
	{
		std::string name;
		std::string detail;        // signature / type shown next to the name
		int kind = 12;             // LSP SymbolKind (Function)
		Protocol::Range range;     // full extent of the declaration
		Protocol::Range selectionRange; // the name token only
		Core::Vector<DocumentSymbol> children;
	};

	class DocumentSymbols
	{
	public:
	/// Builds the symbol tree for one document from its parsed AST. Only declarations in this
	/// file are included; inlined include trees are skipped.
		static Core::Vector<DocumentSymbol> ForDocument(Server::CompilationSession& session, const std::string& uri);

	private:
		static void CollectChildren(Server::CompilationSession& session, const Scripting::AstNode& parent, uint32 sourceId, Core::Vector<DocumentSymbol>& out);
		static void AddDeclaration(Server::CompilationSession& session, const Scripting::AstNode& node, Core::Vector<DocumentSymbol>& out);
		static DocumentSymbol MakeSymbol(Server::CompilationSession& session, const Scripting::AstNode& node, const std::string& name, const std::string& detail, int kind, const Scripting::SourceLocation& selectionStart, const Scripting::SourceLocation& selectionEnd);
		static std::string BuildFunctionDetail(const Scripting::FunctionDefinitionNode& func);
	};
} // namespace C4LSP::Features
