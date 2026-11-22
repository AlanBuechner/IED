#include "Features/DeclarationText.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Macro/MacroLayout.h"
#include "Model/OperatorAnnotations.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/AnnotationDeclarationNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/ForLoopNode.h"
#include "Server/WrittenChildren.h"
#include <algorithm>
#include <unordered_set>

namespace C4LSP::Features
{
	using C4Compiler::NodeType;

	// An initializer longer than this, or on several lines, is left out of a declaration as written.
	static constexpr size_t kMostInitializerBytes = 60;

	static bool Present(const C4Compiler::Token& token) { return token.GetStartLocation().line != 0; }

	static bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

	static size_t SkipSpace(std::string_view text, size_t at)
	{
		while (at < text.size() && IsSpace(text[at]))
			++at;
		return at;
	}

	static size_t LaterOf(size_t at, const Core::Vector<Ref<C4Compiler::AnnotationNode>>& annotations)
	{
		for (const auto& annotation : annotations)
			if (annotation)
				at = std::max<size_t>(at, annotation->endLocation.byteOffset);
		return at;
	}

	// The first `{` or `;` from `at` outside parentheses, brackets, literals and comments; npos when there is none.
	static size_t FindOpening(std::string_view text, size_t at)
	{
		int depth = 0;
		while (at < text.size())
		{
			const char c = text[at];
			if (c == '/' && at + 1 < text.size() && text[at + 1] == '/')
				at = text.find('\n', at);
			else if (c == '/' && at + 1 < text.size() && text[at + 1] == '*')
				at = text.find("*/", at + 2);
			else if (c == '"' || c == '\'')
			{
				for (++at; at < text.size() && text[at] != c; ++at)
					if (text[at] == '\\')
						++at;
			}
			else if (c == '(' || c == '[')
				++depth;
			else if (c == ')' || c == ']')
				--depth;
			else if ((c == '{' || c == ';') && depth <= 0)
				return at;
			if (at == std::string_view::npos)
				return std::string_view::npos;
			++at;
		}
		return std::string_view::npos;
	}

	// The whitespace before `at` on its line, when nothing else is: what continuation lines are indented by too.
	static std::string_view IndentBefore(std::string_view text, size_t at)
	{
		const size_t lineStart = at == 0 ? 0 : text.rfind('\n', at - 1) + 1;
		std::string_view prefix = text.substr(lineStart, at - lineStart);
		return std::all_of(prefix.begin(), prefix.end(), IsSpace) ? prefix : std::string_view();
	}

	std::string_view DeclarationText::SourceText(Server::CompilationSession& session, uint32 sourceId)
	{
		const auto& sources = session.GetSourceManager();
		if (sourceId == 0 || !sources.HasInfo(sourceId) || !sources.GetInfo(sourceId).source)
			return {};
		const auto& source = sources.GetInfo(sourceId).source;
		return std::string_view(source->Data(), source->Size());
	}

	std::string DeclarationText::NameOf(const C4Compiler::AstNode& decl)
	{
		switch (decl.Type())
		{
		case NodeType::Namespace: return static_cast<const C4Compiler::NamespaceNode&>(decl).Name();
		case NodeType::ObjectDefinition: return static_cast<const C4Compiler::ObjectDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::VariableDefinition: return static_cast<const C4Compiler::VariableDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::EnumDefinition: return static_cast<const C4Compiler::EnumDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::EnumMember: return static_cast<const C4Compiler::EnumMemberNode&>(decl).GetNameToken().GetText();
		case NodeType::UnionDefinition: return static_cast<const C4Compiler::UnionDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::AliasDefinition: return static_cast<const C4Compiler::AliasDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::AnnotationDeclaration: return static_cast<const C4Compiler::AnnotationDeclarationNode&>(decl).GetName();
		case NodeType::Parameter: return static_cast<const C4Compiler::ParameterNode&>(decl).GetNameToken().GetText();
		case NodeType::ForLoop: return static_cast<const C4Compiler::ForLoopNode&>(decl).GetRangeVarNameToken().GetText();
		case NodeType::FunctionDefinition: {
			const auto& func = static_cast<const C4Compiler::FunctionDefinitionNode&>(decl);
			return func.GetName() ? func.GetName()->GetLastIdentifierToken().GetText() : std::string();
		}
		default:
			return "";
		}
	}

	std::string DeclarationText::WrittenName(const C4Compiler::AstNode& decl)
	{
		if (decl.Type() != NodeType::FunctionDefinition)
			return NameOf(decl);
		const auto& func = static_cast<const C4Compiler::FunctionDefinitionNode&>(decl);
		if (!func.GetName())
			return "";
		std::string name;
		for (const auto& segment : func.GetName()->GetSegments())
			name += (name.empty() ? "" : "::") + segment.nameToken.GetText();
		return name;
	}

	DeclarationText::NameLocation DeclarationText::NameLocationOf(const C4Compiler::AstNode& decl)
	{
		auto of = [](const C4Compiler::Token& token) { return NameLocation{ token.GetStartLocation(), token.GetEndLocation(), token.GetStartLocation(), token.GetStartLocation().line != 0 }; };
		switch (decl.Type())
		{
		case NodeType::Namespace: return of(static_cast<const C4Compiler::NamespaceNode&>(decl).GetNameToken());
		case NodeType::ObjectDefinition: return of(static_cast<const C4Compiler::ObjectDefinitionNode&>(decl).GetNameToken());
		case NodeType::VariableDefinition: return of(static_cast<const C4Compiler::VariableDefinitionNode&>(decl).GetNameToken());
		case NodeType::EnumDefinition: return of(static_cast<const C4Compiler::EnumDefinitionNode&>(decl).GetNameToken());
		case NodeType::EnumMember: return of(static_cast<const C4Compiler::EnumMemberNode&>(decl).GetNameToken());
		case NodeType::UnionDefinition: return of(static_cast<const C4Compiler::UnionDefinitionNode&>(decl).GetNameToken());
		case NodeType::AliasDefinition: return of(static_cast<const C4Compiler::AliasDefinitionNode&>(decl).GetNameToken());
		case NodeType::AnnotationDeclaration: return of(static_cast<const C4Compiler::AnnotationDeclarationNode&>(decl).GetNameToken());
		case NodeType::Parameter: return of(static_cast<const C4Compiler::ParameterNode&>(decl).GetNameToken());
		case NodeType::ForLoop: {
			const auto& loop = static_cast<const C4Compiler::ForLoopNode&>(decl);
			return loop.IsRangeBased() ? of(loop.GetRangeVarNameToken()) : NameLocation{}; // BUG-407: its variable, not the statement
		}
		case NodeType::FunctionDefinition: {
			const auto& func = static_cast<const C4Compiler::FunctionDefinitionNode&>(decl);
			if (!func.GetName())
				return {};
			NameLocation name = of(func.GetName()->GetFirstIdentifierToken());
			name.end = func.GetName()->GetLastIdentifierToken().GetEndLocation();
			name.simpleStart = func.GetName()->GetLastIdentifierToken().GetStartLocation();
			return name;
		}
		default:
			return {};
		}
	}

	DeclarationText::Span DeclarationText::NameSpan(const C4Compiler::AstNode& decl)
	{
		const NameLocation name = NameLocationOf(decl);
		return name.found ? Span{ name.start.byteOffset, name.end.byteOffset } : Span{};
	}

	// A range-for variable is declared by its loop's first clause, `const Entry& entry` of `for (const Entry& entry : all)`.
	size_t DeclarationText::StartOf(const C4Compiler::AstNode& decl)
	{
		if (decl.Type() == NodeType::ForLoop)
			if (const auto& clause = static_cast<const C4Compiler::ForLoopNode&>(decl).GetInitClause())
				return clause->startLocation.byteOffset;
		return decl.startLocation.byteOffset;
	}

	size_t DeclarationText::AfterAccess(std::string_view text, const C4Compiler::AstNode& decl)
	{
		size_t at = StartOf(decl);
		switch (decl.Type())
		{
		case NodeType::FunctionDefinition: {
			const auto& func = static_cast<const C4Compiler::FunctionDefinitionNode&>(decl);
			at = LaterOf(at, func.GetAnnotations());
			if (Present(func.GetAccessModifierToken()))
				at = std::max<size_t>(at, func.GetAccessModifierToken().GetEndLocation().byteOffset);
			break;
		}
		case NodeType::VariableDefinition: {
			const auto& var = static_cast<const C4Compiler::VariableDefinitionNode&>(decl);
			at = LaterOf(at, var.GetAnnotations());
			if (Present(var.GetAccessModifierToken()))
				at = std::max<size_t>(at, var.GetAccessModifierToken().GetEndLocation().byteOffset);
			break;
		}
		case NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const C4Compiler::ObjectDefinitionNode&>(decl);
			at = LaterOf(at, obj.GetAnnotations());
			if (Present(obj.GetAccessModifierToken()))
				at = std::max<size_t>(at, obj.GetAccessModifierToken().GetEndLocation().byteOffset);
			break;
		}
		case NodeType::EnumDefinition:
			at = static_cast<const C4Compiler::EnumDefinitionNode&>(decl).GetEnumKeywordToken().GetStartLocation().byteOffset;
			break;
		case NodeType::UnionDefinition:
			at = static_cast<const C4Compiler::UnionDefinitionNode&>(decl).GetUnionKeywordToken().GetStartLocation().byteOffset;
			break;
		case NodeType::AliasDefinition:
			at = LaterOf(at, static_cast<const C4Compiler::AliasDefinitionNode&>(decl).GetAnnotations());
			break;
		case NodeType::Parameter:
			at = LaterOf(at, static_cast<const C4Compiler::ParameterNode&>(decl).GetAnnotations());
			break;
		default:
			break;
		}
		return SkipSpace(text, at);
	}

	size_t DeclarationText::HeaderEnd(std::string_view text, const C4Compiler::AstNode& decl)
	{
		const size_t end = std::min<size_t>(decl.endLocation.byteOffset, text.size());
		switch (decl.Type())
		{
		case NodeType::FunctionDefinition: {
			const auto& func = static_cast<const C4Compiler::FunctionDefinitionNode&>(decl);
			if (func.GetBody())
				return func.GetBody()->startLocation.byteOffset;
			return Present(func.GetSemicolonToken()) ? func.GetSemicolonToken().GetStartLocation().byteOffset : end;
		}
		case NodeType::VariableDefinition: {
			const auto& var = static_cast<const C4Compiler::VariableDefinitionNode&>(decl);
			const size_t stop = Present(var.GetSemicolonToken()) ? var.GetSemicolonToken().GetStartLocation().byteOffset : end;
			if (!var.GetInitializer() || !Present(var.GetAssignmentToken()))
				return stop;
			const size_t assignment = var.GetAssignmentToken().GetStartLocation().byteOffset;
			std::string_view initializer = text.substr(assignment, stop > assignment ? stop - assignment : 0);
			const bool fits = initializer.size() <= kMostInitializerBytes && initializer.find('\n') == std::string_view::npos;
			return fits ? stop : assignment;
		}
		case NodeType::ForLoop:
			return NameSpan(decl).end;
		case NodeType::AnnotationDeclaration: {
			const C4Compiler::Token& semicolon = static_cast<const C4Compiler::AnnotationDeclarationNode&>(decl).GetSemicolonToken();
			return Present(semicolon) ? semicolon.GetStartLocation().byteOffset : end;
		}
		case NodeType::Namespace:
		case NodeType::ObjectDefinition:
		case NodeType::EnumDefinition:
		case NodeType::UnionDefinition:
		case NodeType::AliasDefinition: {
			if (decl.Type() == NodeType::Namespace)
				return NameSpan(decl).end;
			const size_t opening = FindOpening(text, NameSpan(decl).end);
			return opening == std::string_view::npos ? end : opening;
		}
		default:
			return end;
		}
	}

	// Continuation lines lose the indentation the first line had in the file; every line loses trailing space.
	static std::string Dedent(const std::string& slice, std::string_view indent)
	{
		std::string out;
		std::string_view rest = slice;
		for (bool first = true;; first = false)
		{
			const size_t newline = rest.find('\n');
			std::string_view line = rest.substr(0, newline);
			size_t strip = 0;
			while (!first && strip < indent.size() && strip < line.size() && line[strip] == indent[strip])
				++strip;
			line.remove_prefix(strip);
			while (!line.empty() && IsSpace(line.back()))
				line.remove_suffix(1);
			out += (first ? "" : "\n") + std::string(line);
			if (newline == std::string_view::npos)
				break;
			rest.remove_prefix(newline + 1);
		}
		while (!out.empty() && IsSpace(out.back()))
			out.pop_back();
		return out;
	}

	static std::string OneLine(const std::string& text)
	{
		std::string out;
		for (char c : text)
		{
			if (!IsSpace(c))
				out += c;
			else if (!out.empty() && out.back() != ' ')
				out += ' ';
		}
		while (!out.empty() && out.back() == ' ')
			out.pop_back();
		return out;
	}

	static bool IsConstructor(const C4Compiler::FunctionDefinitionNode& func)
	{
		Core::Vector<Core::String> operators;
		for (const auto& annotation : func.GetAnnotations())
			if (annotation)
				C4Compiler::CollectOperatorAnnotations(annotation.Get(), func.GetParameters().Count(), operators);
		for (const auto& name : operators)
			if (name == "create")
				return true;
		return false;
	}

	// [begin, end) of the file as its parse read it: a macro use is its expansion (`kLimit = ((64) * 2)`), R8.
	static std::string AsParsed(const C4Compiler::MacroLayout* macros, std::string_view text, size_t begin, size_t end)
	{
		return macros ? macros->Render(text, static_cast<uint32>(begin), static_cast<uint32>(end)) : std::string(text.substr(begin, end - begin));
	}

	const C4Compiler::MacroLayout* DeclarationText::MacrosOf(Server::CompilationSession& session, uint32 sourceId)
	{
		const auto& sources = session.GetSourceManager();
		return sourceId != 0 && sources.HasInfo(sourceId) ? sources.GetInfo(sourceId).macros.Get() : nullptr;
	}

	std::string DeclarationText::Header(std::string_view text, const C4Compiler::MacroLayout* macros, const C4Compiler::AstNode& decl, const std::string& name)
	{
		const size_t start = StartOf(decl);
		const size_t end = std::min(HeaderEnd(text, decl), text.size());
		const Span span = NameSpan(decl);
		if (span.end == 0 || span.start < start || span.end > end)
			return "";
		const std::string slice = AsParsed(macros, text, start, span.start) + name + AsParsed(macros, text, span.end, end);
		return Dedent(slice, IndentBefore(text, start));
	}

	std::string DeclarationText::Written(Server::CompilationSession& session, const C4Compiler::AstNode& decl)
	{
		std::string_view text = SourceText(session, decl.startLocation.sourceId);
		const Span span = NameSpan(decl);
		if (text.empty() || span.end == 0 || span.end > text.size())
			return "";
		return Header(text, MacrosOf(session, decl.startLocation.sourceId), decl, std::string(text.substr(span.start, span.end - span.start)));
	}

	std::string DeclarationText::Qualified(Server::CompilationSession& session, const C4Compiler::AstNode& decl)
	{
		std::string_view text = SourceText(session, decl.startLocation.sourceId);
		if (text.empty())
			return "";
		const std::string owner = OwnerOf(session, decl);
		return Header(text, MacrosOf(session, decl.startLocation.sourceId), decl, owner.empty() ? WrittenName(decl) : owner + "::" + NameOf(decl));
	}

	std::string DeclarationText::Detail(Server::CompilationSession& session, const C4Compiler::AstNode& decl)
	{
		if (decl.Type() == NodeType::Namespace)
			return "namespace";
		std::string_view text = SourceText(session, decl.startLocation.sourceId);
		const Span name = NameSpan(decl);
		if (text.empty() || name.end == 0 || name.end > text.size())
			return "";
		const size_t start = std::min(AfterAccess(text, decl), name.start);
		const bool typeOnly = decl.Type() == NodeType::VariableDefinition || decl.Type() == NodeType::Parameter || decl.Type() == NodeType::ForLoop;
		const size_t end = typeOnly ? name.end : std::min(HeaderEnd(text, decl), text.size());
		const C4Compiler::MacroLayout* macros = MacrosOf(session, decl.startLocation.sourceId);
		std::string kept = AsParsed(macros, text, start, name.start);
		if (end > name.end)
			kept += AsParsed(macros, text, name.end, end);
		return OneLine(kept);
	}

	std::string DeclarationText::AccessOf(const C4Compiler::AstNode& decl)
	{
		switch (decl.Type())
		{
		case NodeType::FunctionDefinition: {
			const auto& token = static_cast<const C4Compiler::FunctionDefinitionNode&>(decl).GetAccessModifierToken();
			return Present(token) ? std::string(token.GetText()) : std::string();
		}
		case NodeType::VariableDefinition: {
			const auto& token = static_cast<const C4Compiler::VariableDefinitionNode&>(decl).GetAccessModifierToken();
			return Present(token) ? std::string(token.GetText()) : std::string();
		}
		case NodeType::ObjectDefinition: {
			const auto& token = static_cast<const C4Compiler::ObjectDefinitionNode&>(decl).GetAccessModifierToken();
			return Present(token) ? std::string(token.GetText()) : std::string();
		}
		default:
			return std::string();
		}
	}

	std::string DeclarationText::OwnerOf(Server::CompilationSession& session, const C4Compiler::AstNode& decl)
	{
		if (decl.Type() == NodeType::Parameter)
			return "";
		std::string owner;
		auto add = [&owner](const std::string& name) {
			if (!name.empty())
				owner += (owner.empty() ? "" : "::") + name;
		};
		const Core::Vector<const C4Compiler::AstNode*> path = PathTo(session, decl);
		for (uint32 i = 0; i + 1 < path.Count(); ++i)
		{
			const C4Compiler::AstNode& node = *path[i];
			if (node.Type() == NodeType::FunctionDefinition)
				return ""; // a local names nothing outside its function
			const bool root = node.Type() == NodeType::Namespace && static_cast<const C4Compiler::NamespaceNode&>(node).IsRoot();
			const bool scope = node.Type() == NodeType::Namespace || node.Type() == NodeType::ObjectDefinition ||
				node.Type() == NodeType::EnumDefinition || node.Type() == NodeType::UnionDefinition;
			if (scope && !root)
				add(NameOf(node));
		}
		if (decl.Type() == NodeType::FunctionDefinition)
		{
			const auto& func = static_cast<const C4Compiler::FunctionDefinitionNode&>(decl);
			if (func.GetName())
			{
				const auto& segments = func.GetName()->GetSegments();
				for (uint32 i = 0; i + 1 < segments.Count(); ++i)
					add(segments[i].nameToken.GetText());
			}
		}
		return owner;
	}

	int DeclarationText::SymbolKindOf(const C4Compiler::AstNode& decl, bool member)
	{
		switch (decl.Type())
		{
		case NodeType::Namespace: return SymbolKinds::Namespace;
		case NodeType::ObjectDefinition: {
			const C4Compiler::ObjectKind kind = static_cast<const C4Compiler::ObjectDefinitionNode&>(decl).Kind();
			if (kind == C4Compiler::ObjectKind::Struct)
				return SymbolKinds::Struct;
			return kind == C4Compiler::ObjectKind::Interface ? SymbolKinds::Interface : SymbolKinds::Class;
		}
		case NodeType::EnumDefinition: return SymbolKinds::Enum;
		case NodeType::EnumMember: return SymbolKinds::EnumMember;
		case NodeType::UnionDefinition:
		case NodeType::AliasDefinition: return SymbolKinds::Class;
		case NodeType::AnnotationDeclaration: return SymbolKinds::Function; // LSP has no annotation kind; its detail says `annotation`
		case NodeType::FunctionDefinition:
			if (!member)
				return SymbolKinds::Function;
			return IsConstructor(static_cast<const C4Compiler::FunctionDefinitionNode&>(decl)) ? SymbolKinds::Constructor : SymbolKinds::Method;
		case NodeType::VariableDefinition:
			if (member)
				return SymbolKinds::Field;
			return static_cast<const C4Compiler::VariableDefinitionNode&>(decl).IsConst() ? SymbolKinds::Constant : SymbolKinds::Variable;
		default:
			return SymbolKinds::Variable;
		}
	}

	bool DeclarationText::IsMember(Server::CompilationSession& session, const C4Compiler::AstNode& decl)
	{
		const Core::Vector<const C4Compiler::AstNode*> path = PathTo(session, decl);
		if (path.Count() >= 2)
		{
			const NodeType parent = path[path.Count() - 2]->Type();
			if (parent == NodeType::ObjectDefinition || parent == NodeType::EnumDefinition || parent == NodeType::UnionDefinition)
				return true;
		}
		if (WrittenName(decl).find("::") == std::string::npos)
			return false;
		std::unordered_set<std::string> namespaces;
		session.GetResolver().CollectNamespacePaths(namespaces);
		return namespaces.count(OwnerOf(session, decl)) == 0;
	}

	const C4Compiler::AstNode& DeclarationText::InItsType(Server::CompilationSession& session, const C4Compiler::AstNode& decl)
	{
		if (decl.Type() != NodeType::FunctionDefinition)
			return decl;
		std::vector<const C4Compiler::AstNode*> declarations;
		session.GetResolver().CollectFunctionDeclarations(&decl, declarations);
		for (const C4Compiler::AstNode* node : declarations)
			if (node && WrittenName(*node).find("::") == std::string::npos)
				return *node;
		return decl;
	}

	Core::Vector<const C4Compiler::AstNode*> DeclarationText::PathTo(Server::CompilationSession& session, const C4Compiler::AstNode& decl)
	{
		Core::Vector<const C4Compiler::AstNode*> path;
		auto& compiler = session.GetCompiler();
		auto holds = [&](uint32 id) {
			auto unit = compiler.GetCompilationUnit(id);
			return unit && unit->GetRootAST() && FindPath(*unit->GetRootAST().Get(), decl, path);
		};
		if (holds(decl.startLocation.sourceId))
			return path;
		for (uint32 id = 1; id <= compiler.SourceCount(); ++id)
			if (id != decl.startLocation.sourceId && holds(id))
				return path;
		return path;
	}

	bool DeclarationText::FindPath(const C4Compiler::AstNode& node, const C4Compiler::AstNode& target, Core::Vector<const C4Compiler::AstNode*>& path)
	{
		const bool root = node.Type() == NodeType::Namespace && static_cast<const C4Compiler::NamespaceNode&>(node).IsRoot();
		if (!root && node.startLocation.sourceId != target.startLocation.sourceId)
			return false; // another file's nodes (a merged unit's included headers) cannot hold it
		path.Push(&node);
		if (&node == &target)
			return true;
		bool found = false;
		Server::ForEachWrittenChild(node, [&](const C4Compiler::AstNode& child) { found = found || FindPath(child, target, path); });
		if (!found)
			path.Pop();
		return found;
	}
} // namespace C4LSP::Features
