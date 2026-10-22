#include "Features/DeclarationText.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
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
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <algorithm>
#include <unordered_set>

namespace C4LSP::Features
{
	using Scripting::NodeType;

	// An initializer longer than this, or on several lines, is left out of a declaration as written.
	static constexpr size_t kMostInitializerBytes = 60;

	static bool Present(const Scripting::Token& token) { return token.GetStartLocation().line != 0; }

	static bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

	static size_t SkipSpace(std::string_view text, size_t at)
	{
		while (at < text.size() && IsSpace(text[at]))
			++at;
		return at;
	}

	static size_t LaterOf(size_t at, const Core::Vector<Ref<Scripting::AnnotationNode>>& annotations)
	{
		for (const auto& annotation : annotations)
			if (annotation)
				at = std::max(at, annotation->endLocation.byteOffset);
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

	std::string DeclarationText::NameOf(const Scripting::AstNode& decl)
	{
		switch (decl.Type())
		{
		case NodeType::Namespace: return static_cast<const Scripting::NamespaceNode&>(decl).Name();
		case NodeType::ObjectDefinition: return static_cast<const Scripting::ObjectDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::VariableDefinition: return static_cast<const Scripting::VariableDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::EnumDefinition: return static_cast<const Scripting::EnumDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::EnumMember: return static_cast<const Scripting::EnumMemberNode&>(decl).GetNameToken().GetText();
		case NodeType::UnionDefinition: return static_cast<const Scripting::UnionDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::AliasDefinition: return static_cast<const Scripting::AliasDefinitionNode&>(decl).GetNameToken().GetText();
		case NodeType::Parameter: return static_cast<const Scripting::ParameterNode&>(decl).GetNameToken().GetText();
		case NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(decl);
			return func.GetName() ? func.GetName()->GetLastIdentifierToken().GetText() : std::string();
		}
		default:
			return "";
		}
	}

	std::string DeclarationText::WrittenName(const Scripting::AstNode& decl)
	{
		if (decl.Type() != NodeType::FunctionDefinition)
			return NameOf(decl);
		const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(decl);
		if (!func.GetName())
			return "";
		std::string name;
		for (const auto& segment : func.GetName()->GetSegments())
			name += (name.empty() ? "" : "::") + segment.nameToken.GetText();
		return name;
	}

	DeclarationText::NameLocation DeclarationText::NameLocationOf(const Scripting::AstNode& decl)
	{
		auto of = [](const Scripting::Token& token) { return NameLocation{ token.GetStartLocation(), token.GetEndLocation(), token.GetStartLocation(), token.GetStartLocation().line != 0 }; };
		switch (decl.Type())
		{
		case NodeType::Namespace: return of(static_cast<const Scripting::NamespaceNode&>(decl).GetNameToken());
		case NodeType::ObjectDefinition: return of(static_cast<const Scripting::ObjectDefinitionNode&>(decl).GetNameToken());
		case NodeType::VariableDefinition: return of(static_cast<const Scripting::VariableDefinitionNode&>(decl).GetNameToken());
		case NodeType::EnumDefinition: return of(static_cast<const Scripting::EnumDefinitionNode&>(decl).GetNameToken());
		case NodeType::EnumMember: return of(static_cast<const Scripting::EnumMemberNode&>(decl).GetNameToken());
		case NodeType::UnionDefinition: return of(static_cast<const Scripting::UnionDefinitionNode&>(decl).GetNameToken());
		case NodeType::AliasDefinition: return of(static_cast<const Scripting::AliasDefinitionNode&>(decl).GetNameToken());
		case NodeType::Parameter: return of(static_cast<const Scripting::ParameterNode&>(decl).GetNameToken());
		case NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(decl);
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

	DeclarationText::Span DeclarationText::NameSpan(const Scripting::AstNode& decl)
	{
		const NameLocation name = NameLocationOf(decl);
		return name.found ? Span{ name.start.byteOffset, name.end.byteOffset } : Span{};
	}

	size_t DeclarationText::AfterAccess(std::string_view text, const Scripting::AstNode& decl)
	{
		size_t at = decl.startLocation.byteOffset;
		switch (decl.Type())
		{
		case NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(decl);
			at = LaterOf(at, func.GetAnnotations());
			if (Present(func.GetAccessModifierToken()))
				at = std::max(at, func.GetAccessModifierToken().GetEndLocation().byteOffset);
			break;
		}
		case NodeType::VariableDefinition: {
			const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(decl);
			at = LaterOf(at, var.GetAnnotations());
			if (Present(var.GetAccessModifierToken()))
				at = std::max(at, var.GetAccessModifierToken().GetEndLocation().byteOffset);
			break;
		}
		case NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(decl);
			at = LaterOf(at, obj.GetAnnotations());
			if (Present(obj.GetAccessModifierToken()))
				at = std::max(at, obj.GetAccessModifierToken().GetEndLocation().byteOffset);
			break;
		}
		case NodeType::EnumDefinition:
			at = static_cast<const Scripting::EnumDefinitionNode&>(decl).GetEnumKeywordToken().GetStartLocation().byteOffset;
			break;
		case NodeType::UnionDefinition:
			at = static_cast<const Scripting::UnionDefinitionNode&>(decl).GetUnionKeywordToken().GetStartLocation().byteOffset;
			break;
		case NodeType::AliasDefinition:
			at = LaterOf(at, static_cast<const Scripting::AliasDefinitionNode&>(decl).GetAnnotations());
			break;
		case NodeType::Parameter:
			at = LaterOf(at, static_cast<const Scripting::ParameterNode&>(decl).GetAnnotations());
			break;
		default:
			break;
		}
		return SkipSpace(text, at);
	}

	size_t DeclarationText::HeaderEnd(std::string_view text, const Scripting::AstNode& decl)
	{
		const size_t end = std::min(decl.endLocation.byteOffset, text.size());
		switch (decl.Type())
		{
		case NodeType::FunctionDefinition: {
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(decl);
			if (func.GetBody())
				return func.GetBody()->startLocation.byteOffset;
			return Present(func.GetSemicolonToken()) ? func.GetSemicolonToken().GetStartLocation().byteOffset : end;
		}
		case NodeType::VariableDefinition: {
			const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(decl);
			const size_t stop = Present(var.GetSemicolonToken()) ? var.GetSemicolonToken().GetStartLocation().byteOffset : end;
			if (!var.GetInitializer() || !Present(var.GetAssignmentToken()))
				return stop;
			const size_t assignment = var.GetAssignmentToken().GetStartLocation().byteOffset;
			std::string_view initializer = text.substr(assignment, stop > assignment ? stop - assignment : 0);
			const bool fits = initializer.size() <= kMostInitializerBytes && initializer.find('\n') == std::string_view::npos;
			return fits ? stop : assignment;
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

	static bool IsConstructor(const Scripting::FunctionDefinitionNode& func)
	{
		Core::Vector<Core::String> operators;
		for (const auto& annotation : func.GetAnnotations())
			if (annotation)
				Scripting::CollectOperatorAnnotations(annotation.Get(), func.GetParameters().Count(), operators);
		for (const auto& name : operators)
			if (name == "create")
				return true;
		return false;
	}

	std::string DeclarationText::Header(std::string_view text, const Scripting::AstNode& decl, const std::string& name)
	{
		const size_t start = decl.startLocation.byteOffset;
		const size_t end = std::min(HeaderEnd(text, decl), text.size());
		const Span span = NameSpan(decl);
		if (span.end == 0 || span.start < start || span.end > end)
			return "";
		const std::string slice = std::string(text.substr(start, span.start - start)) + name + std::string(text.substr(span.end, end - span.end));
		return Dedent(slice, IndentBefore(text, start));
	}

	std::string DeclarationText::Written(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		std::string_view text = SourceText(session, decl.startLocation.sourceId);
		const Span span = NameSpan(decl);
		if (text.empty() || span.end == 0 || span.end > text.size())
			return "";
		return Header(text, decl, std::string(text.substr(span.start, span.end - span.start)));
	}

	std::string DeclarationText::Qualified(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		std::string_view text = SourceText(session, decl.startLocation.sourceId);
		if (text.empty())
			return "";
		const std::string owner = OwnerOf(session, decl);
		return Header(text, decl, owner.empty() ? WrittenName(decl) : owner + "::" + NameOf(decl));
	}

	std::string DeclarationText::Detail(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		if (decl.Type() == NodeType::Namespace)
			return "namespace";
		std::string_view text = SourceText(session, decl.startLocation.sourceId);
		const Span name = NameSpan(decl);
		if (text.empty() || name.end == 0 || name.end > text.size())
			return "";
		const size_t start = std::min(AfterAccess(text, decl), name.start);
		const bool typeOnly = decl.Type() == NodeType::VariableDefinition || decl.Type() == NodeType::Parameter;
		const size_t end = typeOnly ? name.end : std::min(HeaderEnd(text, decl), text.size());
		std::string kept(text.substr(start, name.start - start));
		if (end > name.end)
			kept += std::string(text.substr(name.end, end - name.end));
		return OneLine(kept);
	}

	std::string DeclarationText::OwnerOf(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		if (decl.Type() == NodeType::Parameter)
			return "";
		std::string owner;
		auto add = [&owner](const std::string& name) {
			if (!name.empty())
				owner += (owner.empty() ? "" : "::") + name;
		};
		const Core::Vector<const Scripting::AstNode*> path = PathTo(session, decl);
		for (uint32 i = 0; i + 1 < path.Count(); ++i)
		{
			const Scripting::AstNode& node = *path[i];
			if (node.Type() == NodeType::FunctionDefinition)
				return ""; // a local names nothing outside its function
			const bool root = node.Type() == NodeType::Namespace && static_cast<const Scripting::NamespaceNode&>(node).IsRoot();
			const bool scope = node.Type() == NodeType::Namespace || node.Type() == NodeType::ObjectDefinition ||
				node.Type() == NodeType::EnumDefinition || node.Type() == NodeType::UnionDefinition;
			if (scope && !root)
				add(NameOf(node));
		}
		if (decl.Type() == NodeType::FunctionDefinition)
		{
			const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(decl);
			if (func.GetName())
			{
				const auto& segments = func.GetName()->GetSegments();
				for (uint32 i = 0; i + 1 < segments.Count(); ++i)
					add(segments[i].nameToken.GetText());
			}
		}
		return owner;
	}

	int DeclarationText::SymbolKindOf(const Scripting::AstNode& decl, bool member)
	{
		switch (decl.Type())
		{
		case NodeType::Namespace: return SymbolKinds::Namespace;
		case NodeType::ObjectDefinition: {
			const Scripting::ObjectKind kind = static_cast<const Scripting::ObjectDefinitionNode&>(decl).Kind();
			if (kind == Scripting::ObjectKind::Struct)
				return SymbolKinds::Struct;
			return kind == Scripting::ObjectKind::Interface ? SymbolKinds::Interface : SymbolKinds::Class;
		}
		case NodeType::EnumDefinition: return SymbolKinds::Enum;
		case NodeType::EnumMember: return SymbolKinds::EnumMember;
		case NodeType::UnionDefinition:
		case NodeType::AliasDefinition: return SymbolKinds::Class;
		case NodeType::FunctionDefinition:
			if (!member)
				return SymbolKinds::Function;
			return IsConstructor(static_cast<const Scripting::FunctionDefinitionNode&>(decl)) ? SymbolKinds::Constructor : SymbolKinds::Method;
		case NodeType::VariableDefinition:
			if (member)
				return SymbolKinds::Field;
			return static_cast<const Scripting::VariableDefinitionNode&>(decl).IsConst() ? SymbolKinds::Constant : SymbolKinds::Variable;
		default:
			return SymbolKinds::Variable;
		}
	}

	bool DeclarationText::IsMember(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		const Core::Vector<const Scripting::AstNode*> path = PathTo(session, decl);
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

	const Scripting::AstNode& DeclarationText::InItsType(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		if (decl.Type() != NodeType::FunctionDefinition)
			return decl;
		std::vector<const Scripting::AstNode*> declarations;
		session.GetResolver().CollectFunctionDeclarations(&decl, declarations);
		for (const Scripting::AstNode* node : declarations)
			if (node && WrittenName(*node).find("::") == std::string::npos)
				return *node;
		return decl;
	}

	Core::Vector<const Scripting::AstNode*> DeclarationText::PathTo(Server::CompilationSession& session, const Scripting::AstNode& decl)
	{
		Core::Vector<const Scripting::AstNode*> path;
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

	bool DeclarationText::FindPath(const Scripting::AstNode& node, const Scripting::AstNode& target, Core::Vector<const Scripting::AstNode*>& path)
	{
		const bool root = node.Type() == NodeType::Namespace && static_cast<const Scripting::NamespaceNode&>(node).IsRoot();
		if (!root && node.startLocation.sourceId != target.startLocation.sourceId)
			return false; // another file's nodes (a merged unit's included headers) cannot hold it
		path.Push(&node);
		if (&node == &target)
			return true;
		for (const auto& child : node.Children())
			if (child && FindPath(*child.Get(), target, path))
				return true;
		path.Pop();
		return false;
	}
} // namespace C4LSP::Features
