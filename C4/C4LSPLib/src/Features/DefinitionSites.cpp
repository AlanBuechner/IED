#include "Features/DefinitionSites.h"
#include "Features/DeclarationText.h"
#include "Features/MissingDefinitions.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <algorithm>
#include <cctype>
#include <map>

namespace C4LSP::Features
{
	using C4Compiler::AstNode;
	using C4Compiler::FunctionDefinitionNode;
	using C4Compiler::NamespaceNode;
	using C4Compiler::NodeType;
	using C4Compiler::ObjectDefinitionNode;

	namespace
	{
		bool Present(const C4Compiler::Token& token) { return token.GetStartLocation().line != 0; }

		// ------------------------------------------------------------------------------------------- the file's text --

		// A file's text by lines, each without its line break.
		struct Lines
		{
			std::string text;
			std::vector<std::pair<size_t, size_t>> spans;

			explicit Lines(std::string all) : text(std::move(all))
			{
				size_t start = 0;
				for (size_t i = 0; i <= text.size(); ++i)
				{
					if (i < text.size() && text[i] != '\n')
						continue;
					size_t end = i;
					if (end > start && text[end - 1] == '\r')
						--end;
					spans.emplace_back(start, end);
					start = i + 1;
				}
			}

			size_t Count() const { return spans.size(); }
			std::string_view At(size_t line) const
			{
				return line < spans.size() ? std::string_view(text).substr(spans[line].first, spans[line].second - spans[line].first) : std::string_view();
			}
		};

		std::string_view IndentationOf(std::string_view line)
		{
			size_t n = 0;
			while (n < line.size() && (line[n] == ' ' || line[n] == '\t'))
				++n;
			return line.substr(0, n);
		}

		bool IsBlank(std::string_view line) { return IndentationOf(line).size() == line.size(); }

		std::string_view Trimmed(std::string_view line)
		{
			line.remove_prefix(IndentationOf(line).size());
			while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
				line.remove_suffix(1);
			return line;
		}

		bool StartsWith(std::string_view text, std::string_view prefix) { return text.substr(0, prefix.size()) == prefix; }
		bool EndsWith(std::string_view text, std::string_view suffix) { return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix; }

		// The file's indentation step: its first indented line's.
		std::string IndentUnitOf(const Lines& lines)
		{
			for (size_t i = 0; i < lines.Count(); ++i)
			{
				const std::string_view line = lines.At(i);
				const std::string_view indent = IndentationOf(line);
				if (indent.empty() || IsBlank(line))
					continue;
				return indent[0] == '\t' ? std::string("\t") : std::string(indent.size(), ' ');
			}
			return "\t";
		}

		// The first line of the comment right above `line`, none between: what belongs to the code at `line`.
		size_t AboveItsComment(const Lines& lines, size_t line)
		{
			while (line > 0)
			{
				const std::string_view above = Trimmed(lines.At(line - 1));
				if (StartsWith(above, "//"))
				{
					--line;
					continue;
				}
				if (!EndsWith(above, "*/"))
					break;
				size_t open = line - 1;
				while (open > 0 && lines.At(open).find("/*") == std::string_view::npos)
					--open;
				if (!StartsWith(Trimmed(lines.At(open)), "/*"))
					break; // a comment after code: the code's own line, not one above the definition
				line = open;
			}
			return line;
		}

		Protocol::Position At(size_t line, size_t character) { return Protocol::Position{ static_cast<uint32>(line), static_cast<uint32>(character) }; }

		// ---------------------------------------------------------------------------- the declaration's neighbours --

		// The functions declared in the same body as it, itself among them, in order.
		std::vector<const FunctionDefinitionNode*> DeclaredBeside(Server::CompilationSession& session, const FunctionDefinitionNode& declaration)
		{
			const Core::Vector<const AstNode*> path = DeclarationText::PathTo(session, declaration);
			if (path.Count() < 2)
				return { &declaration };
			const AstNode& parent = *path[path.Count() - 2];
			std::vector<const FunctionDefinitionNode*> out;
			auto take = [&](const Ref<AstNode>& member) {
				if (member && member->Type() == NodeType::FunctionDefinition)
					out.push_back(static_cast<const FunctionDefinitionNode*>(member.Get()));
			};
			if (parent.Type() == NodeType::ObjectDefinition)
				for (const auto& member : static_cast<const ObjectDefinitionNode&>(parent).GetBodyMembers())
					take(member);
			else
				for (const auto& child : parent.Children())
					take(child);
			return out;
		}

		// The type the functions beside it are methods of (null for free functions), and whether a template's.
		struct Beside
		{
			const ObjectDefinitionNode* owner = nullptr;
			bool templated = false;
		};

		Beside BesideOf(const DeclarationOwners& owners)
		{
			Beside beside;
			beside.owner = owners.types.empty() ? nullptr : owners.types.back();
			for (const ObjectDefinitionNode* type : owners.types)
				beside.templated = beside.templated || type->IsTemplated() || type->IsSpecialization();
			return beside;
		}

		// Its body written out of line; null when it has none, or has it where it is declared. A template no use made a
		// function of has its body found by name.
		const FunctionDefinitionNode* DefinitionOf(Server::CompilationSession& session, const FunctionDefinitionNode& declared, const Beside& beside)
		{
			if (declared.IsDefinition())
				return nullptr;
			std::vector<const AstNode*> nodes;
			session.GetResolver().CollectFunctionDeclarations(&declared, nodes);
			for (const AstNode* node : nodes)
				if (node != &declared && node->Type() == NodeType::FunctionDefinition && static_cast<const FunctionDefinitionNode*>(node)->IsDefinition())
					return static_cast<const FunctionDefinitionNode*>(node);
			const bool templated = beside.templated || declared.IsTemplated();
			return templated ? MissingDefinitions::TemplateBodyFor(session, declared, beside.owner) : nullptr;
		}

		std::vector<std::string> NamespacesAround(Server::CompilationSession& session, const AstNode& node)
		{
			std::vector<std::string> out;
			for (const AstNode* around : DeclarationText::PathTo(session, node))
				if (around->Type() == NodeType::Namespace && !static_cast<const NamespaceNode*>(around)->IsRoot())
					out.push_back(static_cast<const NamespaceNode*>(around)->Name());
			return out;
		}

		bool Prefixes(const std::vector<std::string>& prefix, const std::vector<std::string>& all)
		{
			return prefix.size() <= all.size() && std::equal(prefix.begin(), prefix.end(), all.begin());
		}

		// A function declared beside it whose body is written out of line, in namespaces a name can reach it from.
		struct Neighbour
		{
			size_t index = 0;
			const FunctionDefinitionNode* definition = nullptr;
		};

		// `X.ch`'s `X.cf` when the program compiles one; 0 otherwise.
		uint32 SourceBeside(Server::CompilationSession& session, uint32 headerId)
		{
			const std::string uri = session.UriForSourceId(headerId);
			if (uri.size() < 3)
				return 0;
			std::string extension = uri.substr(uri.size() - 3);
			for (char& c : extension)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return extension == ".ch" ? session.FindSourceIdByUri(uri.substr(0, uri.size() - 3) + ".cf") : 0;
		}

		// The file most neighbours are defined in, the source beside the header first among equals; else that source.
		uint32 ChooseFile(const std::vector<Neighbour>& neighbours, uint32 beside)
		{
			std::map<uint32, size_t> counts;
			for (const Neighbour& n : neighbours)
				++counts[n.definition->startLocation.sourceId];
			uint32 best = 0;
			size_t most = 0;
			for (const auto& [id, count] : counts)
				if (count > most || (count == most && id == beside))
				{
					best = id;
					most = count;
				}
			return best != 0 ? best : beside;
		}

		// ------------------------------------------------------------------------------------- how a file writes code --

		bool BraceOnOwnLine(const FunctionDefinitionNode& definition)
		{
			const auto& body = definition.GetBody();
			return body && body->GetOpenBraceToken().GetStartLocation().line > definition.GetCloseParenToken().GetStartLocation().line;
		}

		bool WritesModifiers(const FunctionDefinitionNode& definition)
		{
			return Present(definition.GetAccessModifierToken()) || Present(definition.GetStaticToken());
		}

		// The first body a file writes out of line (`Type::Method`), else the first function body at its namespaces' scope.
		const FunctionDefinitionNode* FirstBodyIn(const AstNode& node, uint32 sourceId, bool outOfLine)
		{
			for (const auto& child : node.Children())
			{
				if (!child || child->startLocation.sourceId != sourceId)
					continue;
				if (child->Type() == NodeType::Namespace)
				{
					if (const FunctionDefinitionNode* found = FirstBodyIn(*child, sourceId, outOfLine))
						return found;
				}
				else if (child->Type() == NodeType::FunctionDefinition)
				{
					const auto& function = static_cast<const FunctionDefinitionNode&>(*child);
					if (function.IsDefinition() && function.GetName() && (!outOfLine || function.GetName()->SegmentCount() > 1))
						return &function;
				}
			}
			return nullptr;
		}

		// The deepest block of `wanted`'s namespaces (outermost first) the file opens, the last of equal depth.
		void FindBlock(const AstNode& node, const std::vector<std::string>& wanted, size_t depth, uint32 sourceId,
			const NamespaceNode*& best, size_t& bestDepth)
		{
			if (depth >= wanted.size())
				return;
			for (const auto& child : node.Children())
			{
				if (!child || child->Type() != NodeType::Namespace || child->startLocation.sourceId != sourceId)
					continue;
				const auto& block = static_cast<const NamespaceNode&>(*child);
				if (std::string(block.Name()) != wanted[depth])
					continue;
				if (depth + 1 >= bestDepth)
				{
					best = &block;
					bestDepth = depth + 1;
				}
				FindBlock(block, wanted, depth + 1, sourceId, best, bestDepth);
			}
		}

		// ---------------------------------------------------------------------------------------------- the sites --

		DefinitionSite NextTo(Server::CompilationSession& session, const Lines& lines, DefinitionSite site, const FunctionDefinitionNode& definition, bool afterIt)
		{
			site.namespaces = NamespacesAround(session, definition);
			const size_t first = definition.startLocation.line - 1;
			site.style.indent = std::string(IndentationOf(lines.At(first)));
			site.style.braceOnOwnLine = BraceOnOwnLine(definition);
			site.style.writesModifiers = WritesModifiers(definition);
			if (afterIt)
			{
				const size_t last = definition.endLocation.line - 1;
				site.at = At(last, lines.At(last).size()); // after anything on its last line (a comment)
				site.lead = "\n\n";
			}
			else
			{
				site.at = At(AboveItsComment(lines, first), 0);
				site.trail = "\n\n";
			}
			return site;
		}

		DefinitionSite InBlockOrAtEnd(Server::CompilationSession& session, const Lines& lines, DefinitionSite site,
			const FunctionDefinitionNode& declaration, const std::vector<std::string>& declaredIn)
		{
			auto unit = session.GetCompiler().FindCompilationUnit(site.sourceId);
			const AstNode* root = unit && unit->GetRootAST() ? unit->GetRootAST().Get() : nullptr;
			const FunctionDefinitionNode* written = root ? FirstBodyIn(*root, site.sourceId, true) : nullptr;
			const FunctionDefinitionNode* anyBody = written ? written : root ? FirstBodyIn(*root, site.sourceId, false) : nullptr;
			site.style.braceOnOwnLine = anyBody && BraceOnOwnLine(*anyBody);
			site.style.writesModifiers = written ? WritesModifiers(*written) : Present(declaration.GetAccessModifierToken());

			const NamespaceNode* block = nullptr;
			size_t depth = 0;
			if (root)
				FindBlock(*root, declaredIn, 0, site.sourceId, block, depth);
			if (block)
			{
				site.namespaces.assign(declaredIn.begin(), declaredIn.begin() + static_cast<std::ptrdiff_t>(depth));
				const size_t closing = block->endLocation.line - 1;
				const std::string_view line = lines.At(closing);
				const size_t brace = std::min<size_t>(block->endLocation.column >= 2 ? block->endLocation.column - 2 : 0, line.size());
				site.style.indent = std::string(IndentationOf(line)) + site.style.indentUnit;
				site.trail = "\n";
				if (IsBlank(line.substr(0, brace)))
				{
					site.at = At(closing, 0);
					const std::string_view above = closing > 0 ? Trimmed(lines.At(closing - 1)) : std::string_view();
					site.lead = above.empty() || above.back() == '{' ? "" : "\n";
				}
				else
				{
					site.at = At(closing, brace); // `}` after code on its line
					site.lead = "\n";
				}
				return site;
			}

			site.wrap = declaredIn;
			site.namespaces = declaredIn; // the definitions are written inside the block it opens
			site.style.indent = declaredIn.empty() ? std::string() : site.style.indentUnit;
			const size_t last = lines.Count() - 1;
			site.at = At(last, lines.At(last).size());
			const bool endsWithBreak = lines.At(last).empty() && last > 0;
			site.lead = lines.text.empty() ? "" : !endsWithBreak ? "\n\n" : IsBlank(lines.At(last - 1)) ? "" : "\n";
			site.trail = "\n";
			return site;
		}
	} // namespace

	std::optional<DefinitionSite> DefinitionSites::InSourceFile(Server::CompilationSession& session, const FunctionDefinitionNode& declaration)
	{
		const DeclarationOwners owners = DefinitionWriter::OwnersOf(session, declaration);
		const std::vector<std::string>& declaredIn = owners.namespaces;
		const Beside around = BesideOf(owners);
		const std::vector<const FunctionDefinitionNode*> beside = DeclaredBeside(session, declaration);
		const size_t self = static_cast<size_t>(std::find(beside.begin(), beside.end(), &declaration) - beside.begin());
		if (self == beside.size())
			return std::nullopt;

		std::vector<Neighbour> neighbours;
		for (size_t i = 0; i < beside.size(); ++i)
			if (i != self)
				if (const FunctionDefinitionNode* definition = DefinitionOf(session, *beside[i], around))
					if (Prefixes(NamespacesAround(session, *definition), declaredIn))
						neighbours.push_back({ i, definition });

		DefinitionSite site;
		site.sourceId = ChooseFile(neighbours, SourceBeside(session, declaration.startLocation.sourceId));
		if (site.sourceId == 0)
			return std::nullopt;
		site.uri = session.UriForSourceId(site.sourceId);
		if (site.uri.empty())
			return std::nullopt; // a script host's file: no editor document is it
		const Lines lines(session.GetEditorText(site.uri));
		site.style.indentUnit = IndentUnitOf(lines);

		const Neighbour* before = nullptr;
		const Neighbour* after = nullptr;
		for (const Neighbour& n : neighbours)
		{
			if (n.definition->startLocation.sourceId != site.sourceId)
				continue;
			if (n.index < self && (!before || n.index > before->index))
				before = &n;
			if (n.index > self && (!after || n.index < after->index))
				after = &n;
		}
		if (before)
			return NextTo(session, lines, std::move(site), *before->definition, true);
		if (after)
			return NextTo(session, lines, std::move(site), *after->definition, false);
		return InBlockOrAtEnd(session, lines, std::move(site), declaration, declaredIn);
	}

	DefinitionStyle DefinitionSites::InPlaceStyle(Server::CompilationSession& session, const FunctionDefinitionNode& declaration)
	{
		DefinitionStyle style;
		const Lines lines(session.GetEditorText(session.UriForSourceId(declaration.startLocation.sourceId)));
		style.indentUnit = IndentUnitOf(lines);
		const uint32 nameLine = declaration.GetName()->GetLastIdentifierToken().GetStartLocation().line;
		style.indent = std::string(IndentationOf(lines.At(nameLine > 0 ? nameLine - 1 : 0)));
		for (const FunctionDefinitionNode* beside : DeclaredBeside(session, declaration))
			if (beside->IsDefinition())
			{
				style.braceOnOwnLine = BraceOnOwnLine(*beside);
				break;
			}
		return style;
	}

	std::string DefinitionSites::Joined(const DefinitionSite& site, const std::vector<WrittenDefinition>& definitions, size_t& caret)
	{
		std::string out = site.lead;
		std::string closing;
		if (!site.wrap.empty())
		{
			std::string name;
			for (const std::string& part : site.wrap)
				name += (name.empty() ? "" : "::") + part;
			out += "namespace " + name + "\n{\n";
			closing = "\n}";
		}
		caret = 0;
		for (size_t i = 0; i < definitions.size(); ++i)
		{
			if (i > 0)
				out += "\n\n";
			else
				caret = out.size() + definitions[i].caret;
			out += definitions[i].text;
		}
		return out + closing + site.trail;
	}

	std::string DefinitionSites::FileName(const std::string& uri)
	{
		const std::string path = Protocol::DocumentUri::FromUri(uri);
		const size_t slash = path.find_last_of("/\\");
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}
} // namespace C4LSP::Features
