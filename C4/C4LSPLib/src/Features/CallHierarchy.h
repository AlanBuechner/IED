#pragma once
#include "Core/Core.h"
#include "Protocol/LspTypes.h"
#include "Features/HierarchyItem.h"
#include <string>
#include <unordered_map>

namespace Scripting { struct AstNode; }

namespace C4LSP::Server { class CompilationSession; }

namespace C4LSP::Features
{
	/// Call hierarchy: the function at a position, the functions that call it, and the functions it calls. A function's
	/// declarations (in its type and out of line) are one function.
	class CallHierarchy
	{
	public:
		/// The function named at `position` (its declaration's name) or called there; empty for anything else.
		static Core::Vector<HierarchyItem> Prepare(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
		/// Each function that calls the item's, with its calls of it.
		static Core::Vector<HierarchyCall> Incoming(Server::CompilationSession& session, const HierarchyItem& item);
		/// Each function the item's body calls, with those calls.
		static Core::Vector<HierarchyCall> Outgoing(Server::CompilationSession& session, const HierarchyItem& item);

	private:
		using Grouped = std::unordered_map<const Scripting::AstNode*, uint32>; // function -> its entry in the result
		static const Scripting::AstNode* FunctionAt(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position);
		static void AddCall(Server::CompilationSession& session, const Scripting::AstNode& function, const Scripting::AstNode& reference, Grouped& grouped, Core::Vector<HierarchyCall>& out);
		static void CollectCalls(Server::CompilationSession& session, const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& pathIn, Grouped& grouped, Core::Vector<HierarchyCall>& out);
	};
} // namespace C4LSP::Features
