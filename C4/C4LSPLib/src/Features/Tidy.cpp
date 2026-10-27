#include "Features/Tidy.h"
#include "Server/WrittenChildren.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/AssignmentExpressionNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include <set>
#include <vector>

namespace C4LSP::Features
{
	// Server-side on/off flag. Defaults OFF: a bare language-server session serves stock compiler
	// diagnostics only (override-only contract - nothing extra appears unless explicitly enabled).
	// The editor plugin's setting feature-tidy is bound to this key and the client pushes {tidy:<value>} at
	// startup plus live via workspace/didChangeConfiguration, so the plugin experience defaults ON.
	static bool s_TidyEnabled = false;

	void Tidy::SetEnabled(bool enabled) { s_TidyEnabled = enabled; }
	bool Tidy::IsEnabled() { return s_TidyEnabled; }


	namespace
	{
		const Scripting::AstNode* DeclarationOf(Server::CompilationSession& session, const Core::Vector<const Scripting::AstNode*>& path, const Scripting::AstNode& node)
		{
			return session.GetResolver().ResolveReference(node, path).declaration;
		}

		// True when two identifier expressions resolve to the SAME declaration node. Resolution drives
		// the compiler's own query verbs through the shared SemanticResolver, so this is exact for
		// resolved names. Unresolved identifiers have no declaration and never match - a parse-error
		// document gets no false positives from tidy.
		bool ResolvesToSameDeclaration(Server::CompilationSession& session, const Core::Vector<const Scripting::AstNode*>& path, const Scripting::AstNode& a, const Scripting::AstNode& b)
		{
			const auto* declA = DeclarationOf(session, path, a);
			if (!declA) return false;
			return (DeclarationOf(session, path, b) == declA); // pointer identity with the same declaration node
		}

		bool IsVariableDefinition(const Scripting::AstNode& node) { return node.Type() == Scripting::NodeType::VariableDefinition; }
		bool IsIdentifierExpression(const Scripting::AstNode& node) { return node.Type() == Scripting::NodeType::IdentifierExpression; }
	} // namespace

	void Tidy::CheckExpressionTautologies(Server::CompilationSession& session, uint32 sourceId, const Core::Vector<const Scripting::AstNode*>& path, const Scripting::AstNode& node, Core::Vector<Protocol::Diagnostic>& out)
	{
		// `==` / `!=` comparisons live in BinaryExpression; plain assignment is its own node type.
		if (node.Type() == Scripting::NodeType::BinaryExpression)
		{
			auto bin = static_cast<const Scripting::BinaryExpressionNode*>(&node);
			const std::string opText = bin->GetOperatorToken().GetText(); // '==' / '!=' as one token
			if (opText != "==" && opText != "!=")
				return;
			const auto& left = *bin->GetLeftOperand();
			const auto& right = *bin->GetRightOperand();
			if (IsIdentifierExpression(left) && IsIdentifierExpression(right) && ResolvesToSameDeclaration(session, path, left, right))
				out.Push(MakeDiag(session, sourceId, node.startLocation, node.endLocation, Protocol::DiagnosticSeverity::Warning,
					"c4-tidy: comparison of a value with itself is tautological"));
			return;
		}

		if (node.Type() == Scripting::NodeType::AssignmentExpression)
		{
			auto assign = static_cast<const Scripting::AssignmentExpressionNode*>(&node);
			const std::string opText = assign->GetOperatorToken().GetText(); // '=' / '+=' etc. as one token
			if (opText != "=")
				return; // compound self-assignment (`v += v`) is meaningful, not a no-op
			const auto& left = *assign->GetLeftOperand();
			const auto& right = *assign->GetRightOperand();
			if (IsIdentifierExpression(left) && IsIdentifierExpression(right) && ResolvesToSameDeclaration(session, path, left, right))
				out.Push(MakeDiag(session, sourceId, node.startLocation, node.endLocation, Protocol::DiagnosticSeverity::Information,
					"c4-tidy: variable is assigned to itself; the assignment has no effect"));
			return;
		}
	}

	void Tidy::CheckUnusedLocals(Server::CompilationSession& session, uint32 sourceId, const Ref<Scripting::AstNode>& root, Core::Vector<Protocol::Diagnostic>& out)
	{
		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit)
			return;

		// Phase 1: gather every LOCAL variable definition with its declared name(s), AND the set of
		// (declaration node, name) pairs that some identifier expression actually references. Both are
		// collected in one path-aware walk so each reference resolves through the shared SemanticResolver
		// against its own enclosing scope chain (the compiler's resolution). The file root is an implicit
		// NamespaceNode, so plain depth counting can't separate globals from locals - instead we track
		// whether a FunctionDefinition ancestor exists (C4 has no nested functions, so everything below
		// one is function-local; parameters are Parameter nodes and never match here). Multi-declarator
		// lines (`int32 a, b;`) declare several names in one node, so we key by the DECLARATOR's name token.
		std::vector<std::pair<const Scripting::AstNode*, const Scripting::Token*>> localNames; // (definition node, declarator name token)
		std::set<std::pair<const Scripting::AstNode*, std::string>> usedRefs;                  // (declaration node, referenced name)

		struct Frame { const Scripting::AstNode* Node; bool InFunction; Core::Vector<const Scripting::AstNode*> Path; };
		std::vector<Frame> stack{{root.Get(), false, {}}};
		while (!stack.empty())
		{
			auto [nodePtr, inFunction, parentPath] = stack.back();
			stack.pop_back();

			Core::Vector<const Scripting::AstNode*> path = parentPath;
			path.Push(nodePtr);

			const bool selfIsFunction = (nodePtr->Type() == Scripting::NodeType::FunctionDefinition);
			if (inFunction && IsVariableDefinition(*nodePtr))
			{
				auto var = static_cast<const Scripting::VariableDefinitionNode*>(nodePtr);
				for (const auto& declarator : var->GetDeclarators())
					localNames.emplace_back(nodePtr, &declarator.m_NameToken);
			}

			if (IsIdentifierExpression(*nodePtr))
			{
				const Server::NodeResolution res = session.GetResolver().ResolveReference(*nodePtr, path);
				if (res.declaration)
					usedRefs.insert({ res.declaration, std::string(static_cast<const Scripting::IdentifierExpressionNode*>(nodePtr)->GetNameToken().GetText()) });
			}

			const bool childInFunction = inFunction || selfIsFunction; // children of a function are local to it
			Server::ForEachWrittenChild(*nodePtr, [&](const Scripting::AstNode& child) { stack.push_back({ &child, childInFunction, path }); });
		}

		// Phase 2: report every local whose (definition node, name) pair no reference resolved to. A
		// multi-declarator line shares ONE declaration node across its names, so the name must match too -
		// pointer identity alone would mark every name on the line as used when any one of them is read.
		for (const auto& [nodePtr, nameToken] : localNames)
		{
			if (usedRefs.count({ nodePtr, std::string(nameToken->GetText()) }))
				continue;
			out.Push(MakeDiag(session, sourceId, nameToken->GetStartLocation(), nameToken->GetStartLocation(), Protocol::DiagnosticSeverity::Warning,
				"c4-tidy: unused local variable '" + std::string(nameToken->GetText()) + "'"));
		}
	}

	Core::Vector<Protocol::Diagnostic> Tidy::ForDocument(Server::CompilationSession& session, const std::string& uri)
	{
		Core::Vector<Protocol::Diagnostic> result;
		if (!s_TidyEnabled)
			return result; // feature off: zero tidy diagnostics, compiler diagnostics untouched

		uint32 targetId = session.FindSourceIdByUri(uri);
		if (targetId == 0)
			return result;

		auto unit = session.GetCompiler().GetCompilationUnit(targetId);
		if (!unit || !unit->GetRootAST())
			return result; // parse failure: the compiler's own errors already describe it

		const auto& root = *unit->GetRootAST();

		// Single top-down pass for expression-level checks (self-assignment / tautology). The AST pool
		// keeps nodes alive until this session is swapped out, so references into it stay valid here.
		std::vector<std::pair<const Scripting::AstNode*, Core::Vector<const Scripting::AstNode*>>> stack{{&root, {}}};
		while (!stack.empty())
		{
			auto [node, parentPath] = stack.back();
			stack.pop_back();
			Core::Vector<const Scripting::AstNode*> path = parentPath;
			path.Push(node);
			CheckExpressionTautologies(session, targetId, path, *node, result);
			Server::ForEachWrittenChild(*node, [&](const Scripting::AstNode& child) { stack.push_back({ &child, path }); });
		}

		CheckUnusedLocals(session, targetId, unit->GetRootAST(), result);
		return result;
	}

	Protocol::Diagnostic Tidy::MakeDiag(const Server::CompilationSession& session, uint32 sourceId, const Scripting::SourceLocation& start, const Scripting::SourceLocation& end, Protocol::DiagnosticSeverity severity, const std::string& message)
	{
		Protocol::Diagnostic diag;
		diag.range = session.ToEditorRange(sourceId, start, end);
		diag.severity = severity;
		diag.message = message;
		diag.source = "c4-tidy"; // distinct from compiler diagnostics ("c4") so clients can filter by source
		return diag;
	}
} // namespace C4LSP::Features
