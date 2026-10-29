#include "Features/WorkspaceSymbols.h"
#include "Features/DeclarationText.h"
#include "Features/MacroUses.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>
#include <vector>
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"

namespace C4LSP::Features
{
	using Scripting::NodeType;

	// Cap so a large project (Bedrock + user code) cannot flood the client's symbol picker.
	static constexpr size_t kMaxResults = 500;

	static std::string ToLower(const std::string& text)
	{
		std::string result = text;
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

	static bool Matches(const std::string& name, const std::string& query)
	{
		return query.empty() || ToLower(name).find(ToLower(query)) != std::string::npos;
	}

	static std::string Joined(const std::string& outer, const std::string& inner)
	{
		return outer.empty() ? inner : (inner.empty() ? outer : outer + "::" + inner);
	}

	static bool EndsWith(const std::string& text, const char* suffix)
	{
		const size_t length = std::char_traits<char>::length(suffix);
		return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
	}

	// Lower is better: the name itself, then any case, then a prefix, then a part; each with C4 sources before C headers.
	static int Rank(const WorkspaceSymbol& symbol, const std::string& query, const std::string& lowerQuery)
	{
		const std::string lower = ToLower(symbol.name);
		const int match = symbol.name == query ? 0 : (lower == lowerQuery ? 1 : (lower.rfind(lowerQuery, 0) == 0 ? 2 : 3));
		const bool c4 = EndsWith(symbol.location.uri, ".cf") || EndsWith(symbol.location.uri, ".ch");
		return match * 2 + (c4 ? 0 : 1);
	}

	// A large program (the Windows headers' thousands of names) holds more matches than the cap: the best are kept.
	static void KeepBest(Core::Vector<WorkspaceSymbol>& symbols, const std::string& query)
	{
		const std::string lowerQuery = ToLower(query);
		std::vector<std::pair<int, uint32>> order; // (rank, index): ranked once, sorted stably by the index
		order.reserve(symbols.Count());
		for (uint32 i = 0; i < symbols.Count(); ++i)
			order.emplace_back(Rank(symbols[i], query, lowerQuery), i);
		std::sort(order.begin(), order.end());
		Core::Vector<WorkspaceSymbol> best;
		for (size_t i = 0; i < order.size() && i < kMaxResults; ++i)
			best.Push(std::move(symbols[order[i].second]));
		symbols = std::move(best);
	}

	Core::Vector<WorkspaceSymbol> WorkspaceSymbols::ForQuery(Server::CompilationSession& session, const std::string& query)
	{
		Walk walk{ session, query };
		walk.limit = query.empty() ? kMaxResults : SIZE_MAX;
		session.GetResolver().CollectNamespacePaths(walk.namespaces);
		auto& compiler = session.GetCompiler();
		for (uint32 sourceId = 1; sourceId <= compiler.SourceCount() && walk.out.Count() < walk.limit; ++sourceId)
		{
			if (!compiler.GetSourceManager().HasInfo(sourceId))
				continue;
			auto unit = compiler.GetCompilationUnit(sourceId);
			if (unit && unit->GetRootAST())
				for (const auto& child : unit->GetRootAST()->Children())
					if (child && child->startLocation.sourceId == sourceId)
						AddDeclaration(walk, *child.Get(), sourceId, "", false);
		}
		AddMacros(walk);
		if (!query.empty())
			KeepBest(walk.out, query);
		return walk.out;
	}

	void WorkspaceSymbols::AddMacros(Walk& walk)
	{
		for (uint32 sourceId = 1; sourceId <= walk.session.GetCompiler().SourceCount() && walk.out.Count() < walk.limit; ++sourceId)
		{
			const std::string_view text = walk.session.RawTextOf(sourceId);
			if (text.find("define") == std::string_view::npos)
				continue; // most files define none
			const std::string uri = walk.session.UriForSourceId(sourceId);
			if (uri.empty())
				continue;
			for (const MacroUses::Definition& macro : MacroUses::DefinitionsIn(text))
				if (walk.out.Count() < walk.limit && Matches(macro.name, walk.query))
					walk.out.Push(WorkspaceSymbol{ macro.name, SymbolKinds::Function, Protocol::Location{ uri, macro.nameRange }, "" });
		}
	}

	void WorkspaceSymbols::AddEach(Walk& walk, const Core::Vector<Ref<Scripting::AstNode>>& nodes, uint32 sourceId, const std::string& container, bool member)
	{
		for (const auto& node : nodes)
			if (node && node->startLocation.sourceId == sourceId && walk.out.Count() < walk.limit)
				AddDeclaration(walk, *node.Get(), sourceId, container, member);
	}

	void WorkspaceSymbols::AddDeclaration(Walk& walk, const Scripting::AstNode& node, uint32 sourceId, const std::string& container, bool member)
	{
		switch (node.Type())
		{
		case NodeType::Namespace: {
			const auto& ns = static_cast<const Scripting::NamespaceNode&>(node);
			if (!ns.IsRoot())
				Push(walk, node, container, false);
			// Keep descending when the namespace did not match: "Player" must still find Player inside it.
			const std::string inner = ns.IsRoot() ? container : Joined(container, ns.Name());
			for (const auto& child : node.Children())
				if (child && child->startLocation.sourceId == sourceId && walk.out.Count() < walk.limit)
					AddDeclaration(walk, *child.Get(), sourceId, inner, false);
			break;
		}
		case NodeType::ObjectDefinition: {
			Push(walk, node, container, member);
			// Always descend: a query for a field name ("x") must find Point.x though "Point" does not match.
			AddEach(walk, static_cast<const Scripting::ObjectDefinitionNode&>(node).GetBodyMembers(), sourceId, Joined(container, DeclarationText::NameOf(node)), true);
			break;
		}
		case NodeType::EnumDefinition: {
			Push(walk, node, container, member);
			const std::string inner = Joined(container, DeclarationText::NameOf(node));
			for (const auto& value : static_cast<const Scripting::EnumDefinitionNode&>(node).GetMembers())
				if (value)
					Push(walk, *value.Get(), inner, true);
			break;
		}
		case NodeType::FunctionDefinition: {
			// An out-of-line definition (`Counter::Next`) belongs to what qualifies it: a type, unless it names a namespace.
			const std::string written = DeclarationText::WrittenName(node);
			const size_t qualified = written.rfind("::");
			if (qualified == std::string::npos)
			{
				Push(walk, node, container, member);
				break;
			}
			const std::string owner = Joined(container, written.substr(0, qualified));
			Push(walk, node, owner, member || walk.namespaces.count(owner) == 0);
			break;
		}
		case NodeType::VariableDefinition:
		case NodeType::UnionDefinition:
		case NodeType::AliasDefinition:
			Push(walk, node, container, member);
			break;
		default:
			break; // statements and expressions are not workspace symbols
		}
	}

	void WorkspaceSymbols::Push(Walk& walk, const Scripting::AstNode& node, const std::string& container, bool member)
	{
		const std::string name = DeclarationText::NameOf(node);
		if (walk.out.Count() >= walk.limit || name.empty() || !Matches(name, walk.query))
			return;
		const DeclarationText::NameLocation location = DeclarationText::NameLocationOf(node);
		WorkspaceSymbol symbol;
		symbol.name = name;
		symbol.kind = DeclarationText::SymbolKindOf(node, member);
		symbol.location = location.found ? walk.session.ToEditorLocation(location.simpleStart, location.end) : walk.session.ToEditorLocation(node.startLocation, node.endLocation);
		symbol.containerName = container;
		walk.out.Push(symbol);
	}
} // namespace C4LSP::Features
