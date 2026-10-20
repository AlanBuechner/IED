#include "Features/Definition.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Server/NodeLocator.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Features/TypeDefinition.h"
#include <cctype>
#include <set>

namespace C4LSP::Features
{
	Core::Vector<Protocol::Location> Definition::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		// An include directive beats symbol resolution: the caret is on `include "path.ch"` (the
		// keyword or the path string), so jump to that file instead of trying to resolve a symbol.
		auto includeLocations = IncludeTarget(session, uri, position);
		if (includeLocations.Count() > 0)
			return includeLocations;

		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return {};

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		if (!located.Found())
			return {};

		auto locations = ResolveReference(session, sourceId, located);
		if (locations.Count() > 0)
			return locations;

		// Fallback: the caret is on a type name in a declaration (e.g. `ID3D12Device* g_dev`), not a
		// value reference. VS's F12 jumps to the type definition, so do the same here. This also
		// makes F12 work on CBind-generated types whose definitions live in the appended bindings.
		auto typeLocations = TypeDefinition::At(session, uri, position);
		if (typeLocations.Count() > 0)
			return typeLocations;

		// Last resort: the symbol is defined in a file this one does not include (C4 has strict
		// per-unit visibility, so it is unresolved here), but it exists elsewhere in the project.
		// VS Code still jumps to such a definition, so search every unit's symbol table by name.
		return ProjectWideByName(session, located);
	}

	Core::Vector<Protocol::Location> Definition::IncludeTarget(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};

		// Include directives are expanded out of the merged AST by MergeIncludedASTs, so they cannot be
		// found by walking nodes after a recompile. Instead resolve from the editor text exactly like a
		// real IDE: take the line under the caret, and if it is an `include "path.ch"` statement, extract
		// that quoted path - the same string the compiler feeds to ResolveIncludePath when it merges.
		std::string text = session.GetEditorText(uri);
		if (text.empty())
			return {};

		size_t lineStart = 0;
		uint32 currentLine = 0;
		while (currentLine < position.line && lineStart <= text.size())
		{
			size_t nl = text.find('\n', lineStart);
			if (nl == std::string::npos) break;
			lineStart = nl + 1;
			++currentLine;
		}
		if (currentLine != position.line)
			return {}; // caret beyond the last line

		size_t lineEnd = text.find('\n', lineStart);
		std::string line = text.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);

		// Strip leading whitespace; the statement must begin with the `include` keyword.
		size_t i = 0;
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
		if (line.compare(i, 7, "include") != 0)
			return {}; // not an include statement on this line
		size_t kwEnd = i + 7;
		if (kwEnd < line.size() && !isspace(static_cast<unsigned char>(line[kwEnd])))
			return {}; // a longer identifier such as `includex`, not the keyword
		i = kwEnd;

		// The token after `include` must be a quoted string literal. Find the path between the quotes.
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
		if (i >= line.size() || line[i] != '"')
			return {}; // include without a quoted path (already an error); nothing to navigate to
		size_t quoteOpen = i + 1;
		size_t quoteClose = line.find('"', quoteOpen);
		if (quoteClose == std::string::npos)
			return {};

		std::string path = line.substr(quoteOpen, quoteClose - quoteOpen);
		if (path.empty())
			return {};

		// Resolve exactly like the compiler does (requester-relative, -I dirs, basename fallback).
		uint32 targetId = session.GetSourceManager().ResolveIncludePath(path, sourceId);
		if (targetId == 0)
			return {}; // unresolvable include; leave it to diagnostics

		// Point at the top of the included file so Monaco opens and focuses it.
		Protocol::Location target;
		target.uri = session.UriForSourceId(targetId);
		target.range.start.line = 0;
		target.range.start.character = 0;
		target.range.end.line = 0;
		target.range.end.character = 0;
		return { target };
	}

	Core::Vector<Protocol::Location> Definition::ProjectWideByName(Server::CompilationSession& session, const Server::LocatedNode& located)
	{
		// The simple identifier under the caret: a value reference (identifier / member name), or the
		// base name of a type node.
		std::string simpleName;
		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0 && simpleName.empty(); --i)
		{
			const Scripting::AstNode* node = located.path[i];
			switch (node->Type())
			{
			case Scripting::NodeType::IdentifierExpression:
				simpleName = static_cast<const Scripting::IdentifierExpressionNode&>(*node).GetName();
				break;
			case Scripting::NodeType::MemberAccess:
				simpleName = static_cast<const Scripting::MemberAccessNode&>(*node).GetMemberNameToken().GetText();
				break;
			case Scripting::NodeType::QualType: {
				const auto& qt = static_cast<const Scripting::QualTypeNode&>(*node);
				if (qt.BaseName())
					simpleName = qt.BaseName()->GetSegments().Back().nameToken.GetText();
				break;
			}
			default:
				break;
			}
		}
		if (simpleName.empty())
			return {};

		// The model is program-wide and every unit shares it, so one scope-tree scan covers the whole
		// project - no per-unit loop needed. A name declared in a file this one does not include is
		// still found here (build-time visibility), exactly like VS Code jumping to such a definition.
		std::vector<const Scripting::AstNode*> decls;
		session.GetResolver().CollectDeclarationsByName(simpleName, decls);

		std::set<const Scripting::AstNode*> seen;
		Core::Vector<Protocol::Location> result;
		for (const Scripting::AstNode* decl : decls)
			if (seen.insert(decl).second) // same declaration reachable through several units' merged trees
				result.Push(LocationForDeclaration(session, *decl));
		return result;
	}

	const Scripting::AstNode* Definition::FindTargetDeclaration(Server::CompilationSession& session, uint32 sourceId, const Server::LocatedNode& located)
	{
		Server::SemanticResolver& resolver = session.GetResolver();

		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = located.path[i];

			if (node->Type() == Scripting::NodeType::IdentifierExpression ||
				node->Type() == Scripting::NodeType::MemberAccess ||
				node->Type() == Scripting::NodeType::StaticAccess)
			{
				if (const Scripting::AstNode* declaration = resolver.ResolveReference(*node, located.path).declaration)
					return declaration;
			}

			// A call's callee is itself a reference node; jumping from the argument list or
			// parentheses should still land on the function definition.
			if (node->Type() == Scripting::NodeType::FunctionCall)
			{
				const auto& call = static_cast<const Scripting::FunctionCallNode&>(*node);
				if (call.GetCallee())
					if (const Scripting::AstNode* declaration = resolver.ResolveReference(*call.GetCallee().Get(), located.path).declaration)
						return declaration;
			}
		}
		return nullptr;
	}

	Core::Vector<Protocol::Location> Definition::ResolveReference(Server::CompilationSession& session, uint32 sourceId, const Server::LocatedNode& located)
	{
		if (const Scripting::AstNode* declaration = FindTargetDeclaration(session, sourceId, located))
			return { LocationForDeclaration(session, *declaration) };
		return {};
	}

	Protocol::Location Definition::LocationForDeclaration(Server::CompilationSession& session, const Scripting::AstNode& declaration)
	{
		const Scripting::SourceLocation* start = &declaration.startLocation;
		const Scripting::SourceLocation* end = &declaration.endLocation;

		switch (declaration.Type())
		{
		case Scripting::NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(declaration);
			if (func.GetName())
			{
				const auto& nameToken = func.GetName()->GetFirstIdentifierToken();
				start = &nameToken.GetStartLocation();
				end = &nameToken.GetEndLocation();
			}
			break;
		}
		case Scripting::NodeType::VariableDefinition: {
			const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(declaration);
			start = &var.GetNameToken().GetStartLocation();
			end = &var.GetNameToken().GetEndLocation();
			break;
		}
		case Scripting::NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(declaration);
			start = &obj.GetNameToken().GetStartLocation();
			end = &obj.GetNameToken().GetEndLocation();
			break;
		}
		case Scripting::NodeType::EnumDefinition: {
			const auto& en = static_cast<const Scripting::EnumDefinitionNode&>(declaration);
			start = &en.GetNameToken().GetStartLocation();
			end = &en.GetNameToken().GetEndLocation();
			break;
		}
		default:
			break;
		}

		uint32 declSourceId = start->sourceId;
		return session.ToEditorLocation(*start, *end);
	}
} // namespace C4LSP::Features
