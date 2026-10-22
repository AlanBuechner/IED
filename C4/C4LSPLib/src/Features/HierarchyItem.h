#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include <string>

namespace Scripting { struct AstNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// A function or type in a call or type hierarchy (LSP CallHierarchyItem, TypeHierarchyItem).
	struct HierarchyItem
	{
		std::string name;               // the simple name
		int kind = 12;                  // LSP SymbolKind
		std::string detail;             // the name qualified with its owner: `Ai::ToolRegistry::Add`
		std::string uri;
		Protocol::Range range;          // the whole declaration
		Protocol::Range selectionRange; // its name

		static HierarchyItem Of(Server::CompilationSession& session, const Scripting::AstNode& declaration);
	};

	/// An item and the calls that link it to the item asked about, in the calling function's document.
	struct HierarchyCall
	{
		HierarchyItem item;
		Core::Vector<Protocol::Range> fromRanges;
	};
} // namespace C4LSP::Features
