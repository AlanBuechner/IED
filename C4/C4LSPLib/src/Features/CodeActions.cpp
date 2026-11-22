#include "Features/CodeActions.h"
#include "Server/WrittenChildren.h"
#include "Server/CompilationSession.h"
#include "Server/NodeLocator.h" // LocatedNode.path = root->node ancestor chain (innermost last)
#include "Features/Diagnostics.h" // public ForDocument: this file's LSP diagnostics, widened + deduped
#include "Features/DefinitionSites.h"
#include "Features/DefinitionWriter.h"
#include "Features/MissingDefinitions.h"
#include "Features/DeclarationText.h"
#include "SourceManager/Source.h"
#include "Parser/AstNode.h"
#include "Lexer/Token.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>

namespace C4LSP::Features
{
	// ------------------------------------------------------------------ state --

	static bool s_QuickfixesEnabled = true;       // plugin default on; the client pushes live overrides
	static bool s_CreateDefinitionEnabled = true;

	void CodeActions::SetQuickfixesEnabled(bool enabled) { s_QuickfixesEnabled = enabled; }
	bool CodeActions::QuickfixesEnabled() { return s_QuickfixesEnabled; }
	void CodeActions::SetCreateDefinitionEnabled(bool enabled) { s_CreateDefinitionEnabled = enabled; }
	bool CodeActions::CreateDefinitionEnabled() { return s_CreateDefinitionEnabled; }

	// ---------------------------------------------------------------- helpers --

	static bool IsIdentChar(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
	}

	static bool IsIdentifier(const std::string& s)
	{
		if (s.empty() || !IsIdentChar(s.front())) return false;
		for (char c : s) if (!IsIdentChar(c)) return false;
		return true;
	}

	bool CodeActions::IsUndefinedIdentifierMessage(const std::string& message, std::string& nameOut)
	{
		// Keyed to the compiler's OWN diagnostic text (C4Lib/src/Model/SymbolQueries.cpp): an undefined
		// free-function call is reported as "no function named <Name>". The LSP adds no error logic of its
		// own - it only recognizes this one message shape so it can offer a fix for it. This is the single
		// point where the LSP couples to diagnostic text; if the compiler's wording changes, update it here
		// (and IsPointerDotMessage below) so the LSP keeps tracking exactly what C4.exe reports.
		static const char* prefix = "no function named ";
		const size_t plen = std::char_traits<char>::length(prefix);
		if (message.size() <= plen)
		{
			nameOut.clear();
			return false;
		}
		std::string rest = message.substr(plen);
		size_t end = 0; // the compiler emits exactly "<prefix><Name>"; take the leading identifier run
		while (end < rest.size() && IsIdentChar(rest[end])) ++end;
		if (end == 0)
		{
			nameOut.clear();
			return false;
		}
		std::string name = rest.substr(0, end);
		if (!IsIdentifier(name)) { nameOut.clear(); return false; }
		nameOut = std::move(name);
		return true;
	}

	static bool IsPointerDotMessage(const std::string& message)
	{
		return message.find("Cannot use '.' on a pointer") != std::string::npos;
	}

	/// The compiler's dot-on-pointer error reads:
	///   "Cannot use '.' on a pointer of type 'Point'. Dereference it with '->' instead (e.g. `ptr->field`)."
	/// Extract the member name from the backticked example (the text after the last '->').
	static bool ParsePointerDotMember(const std::string& message, std::string& memberOut)
	{
		const size_t open = message.find('`');
		if (open == std::string::npos) { memberOut.clear(); return false; }
		const size_t close = message.find('`', open + 1);
		if (close == std::string::npos || close <= open + 1) { memberOut.clear(); return false; }
		std::string example = message.substr(open + 1, close - open - 1); // e.g. "ptr->field"
		const size_t arrow = example.rfind("->");
		if (arrow == std::string::npos) { memberOut.clear(); return false; }
		std::string member = example.substr(arrow + 2);
		while (!member.empty() && !IsIdentChar(member.back())) member.pop_back(); // trim stray chars
		if (!IsIdentifier(member)) { memberOut.clear(); return false; }
		memberOut = std::move(member);
		return true;
	}

	/// Collect every non-arrow MemberAccessNode in `sourceId` whose member name == `member`, into out.
	/// A node is only considered when its own start location belongs to the target source: a merged unit
	/// inlines included files, so subtrees from other sources are descended but never matched.
	static void CollectDotMemberAccesses(const C4Compiler::AstNode& node, uint32 sourceId,
		const std::string& member, Core::Vector<const C4Compiler::MemberAccessNode*>& out)
	{
		if (node.startLocation.sourceId == sourceId && node.Type() == C4Compiler::NodeType::MemberAccess)
		{
			const auto* ma = static_cast<const C4Compiler::MemberAccessNode*>(&node);
			if (!ma->IsArrowAccess() && ma->GetMemberNameToken().GetText() == member)
				out.Push(ma);
		}

		Server::ForEachWrittenChild(node, [&](const C4Compiler::AstNode& child) { CollectDotMemberAccesses(child, sourceId, member, out); });
	}

	// ------------------------------------------------------------- quickfix --

	static FixedDiagnostic Fixing(const Protocol::Diagnostic& diag)
	{
		return FixedDiagnostic{ diag.range, static_cast<int>(diag.severity), diag.message };
	}

	static void AddPointerQuickfix(Server::CompilationSession& session, uint32 sourceId, const std::string& uri,
		const Protocol::Diagnostic& diag, Core::Vector<CodeAction>& out)
	{
		std::string member;
		if (!ParsePointerDotMember(diag.message, member))
			return; // message shape changed - do not guess

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST()) return;

		uint32 targetLine1Based = diag.range.start.line + 1; // LSP 0-based -> SourceLocation 1-based

		Core::Vector<const C4Compiler::MemberAccessNode*> matches;
		CollectDotMemberAccesses(*unit->GetRootAST(), sourceId, member, matches);
		if (matches.Count() == 0)
			return; // cannot locate the exact access - no safe edit

		// Prefer a match whose dot token sits on the diagnostic's line; otherwise take the first.
		const C4Compiler::MemberAccessNode* chosen = matches[0];
		for (const auto* ma : matches)
			if (ma->GetAccessOperatorToken().GetStartLocation().line == targetLine1Based) { chosen = ma; break; }

		const C4Compiler::Token& dot = chosen->GetAccessOperatorToken();
		CodeAction action;
		action.title = "Use '->' (pointer dereference)";
		action.kind = "quickfix";
		action.diagnostics.Push(Fixing(diag)); // the squiggle itself is where the edit lands
		action.edits.Push({ uri, session.ToEditorRange(sourceId, dot.GetStartLocation(), dot.GetEndLocation()), "->" });
		out.Push(std::move(action));
	}

	// ------------------------------------------------------ create definition --

	/// Text check: is the identifier token starting at (line0, col0) in `text` used as a free-function call?
	/// i.e. its next non-whitespace character after the name is '(' and it is not preceded by '.' or '->'.
	static bool IsFreeFunctionCallAt(const std::string& text, uint32 line0, uint32 col0)
	{
		std::vector<size_t> lineStarts; lineStarts.push_back(0);
		for (size_t i = 0; i < text.size(); ++i)
			if (text[i] == '\n') lineStarts.push_back(i + 1);
		if (line0 >= static_cast<uint32>(lineStarts.size())) return false;

		size_t offset = lineStarts[line0] + col0; // first char of the identifier token (widened start)
		if (offset >= text.size()) return false;

		// Walk back to the identifier's real first character.
		size_t identStart = offset;
		while (identStart > lineStarts[line0] && IsIdentChar(text[identStart - 1])) --identStart;

		// Forward: skip the name, then the next non-whitespace char must be '(' (a call).
		size_t i = offset;
		while (i < text.size() && IsIdentChar(text[i])) ++i;
		while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
		if (i >= text.size() || text[i] != '(') return false; // variable use, type name, etc.

		// Backward: must not be a member access ('.' or '->') on the identifier.
		size_t j = identStart;
		while (j > lineStarts[line0] && std::isspace(static_cast<unsigned char>(text[j - 1]))) --j;
		if (j >= 2 && text[j - 1] == '.') return false;
		if (j >= 3 && text[j - 1] == '>' && text[j - 2] == '-') return false;

		return true;
	}

	/// Where a new file-scope definition for `name` must be inserted so the resulting document compiles:
	///   * use at global scope            -> line start of the anchor line (already above any later use);
	///   * use inside a function/object    -> immediately BEFORE that top-level definition, because C4 does
	///                                       not link nested definitions and requires declarations before use.
	/// Returns false when no safe file-scope point exists (the caller then emits no action for this name).
	static bool InsertionPointFor(Server::CompilationSession& session, uint32 sourceId,
		const Ref<C4Compiler::AstNode>& rootAst, const Protocol::Position& anchor /*0-based LSP*/,
		Protocol::Range& insertAtOut) // zero-width insertion range
	{
		const C4Compiler::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, rootAst,
			anchor);

		if (!located.Found())
		{
			insertAtOut.start.line = anchor.line;
			insertAtOut.end.line = anchor.line;
			insertAtOut.start.character = 0;
			insertAtOut.end.character = 0;
			return true; // broken tree: line-start fallback (valid for global-scope uses)
		}

		// Walk the ancestor path (innermost last) for the OUTERMOST enclosing top-level definition.
		const C4Compiler::AstNode* enclosingDef = nullptr; // FunctionDefinition or ObjectDefinition
		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			auto t = located.path[i]->Type();
			if (t == C4Compiler::NodeType::FunctionDefinition || t == C4Compiler::NodeType::ObjectDefinition)
				enclosingDef = located.path[i]; // keep walking: a class inside a function wins
		}

		if (!enclosingDef)
		{
			insertAtOut.start.line = anchor.line;
			insertAtOut.end.line = anchor.line;
			insertAtOut.start.character = 0;
			insertAtOut.end.character = 0;
			return true; // global scope: line start of the first use
		}

		if (enclosingDef->startLocation.sourceId != sourceId)
			return false; // definition lives in another (included) file - cannot edit it from here

		insertAtOut = session.ToEditorRange(sourceId, enclosingDef->startLocation,
			C4Compiler::SourceLocation{ enclosingDef->startLocation.line, 1, 0, sourceId });
		insertAtOut.end = insertAtOut.start; // zero-width point at the definition's first column
		return true;
	}

	static void AddCreateDefinitionActions(Server::CompilationSession& session, uint32 sourceId, const std::string& uri,
		const Ref<C4Compiler::AstNode>& rootAst, const std::string& editorText,
		const Core::Vector<Protocol::Diagnostic>& diagnostics, Core::Vector<CodeAction>& out)
	{
		// One action per undefined-call diagnostic occurrence: each error line's lightbulb offers the fix for ITS OWN marker
		// (Monaco scopes quickfixes to their squiggle), exactly like VS2022.
		for (const auto& diag : diagnostics)
		{
			if (diag.severity != Protocol::DiagnosticSeverity::Error) continue;
			std::string name;
			if (!CodeActions::IsUndefinedIdentifierMessage(diag.message, name)) continue;

			const Protocol::Position& anchor = diag.range.start; // 0-based LSP position of the identifier
			if (!IsFreeFunctionCallAt(editorText, anchor.line, anchor.character))
				continue; // not a free-function call: no stub can fix this use

			Protocol::Range insertAt;
			if (!InsertionPointFor(session, sourceId, rootAst, anchor, insertAt))
				continue; // cannot find a safe file-scope insertion point

			CodeAction action;
			action.title = "Create definition of '" + name + "'";
			action.kind = "quickfix"; // marker-scoped: the inline lightbulb filters out source.* actions
			action.diagnostics.Push(Fixing(diag)); // squiggle on the undefined identifier (where the error reads)
			// Zero-width: pure insertion at file scope. A minimal, always-compilable C4 signature (variadic body verified
			// to link); the user refines it.
			action.edits.Push({ uri, insertAt, "void " + name + "(...) {\n}\n\n" });
			out.Push(std::move(action));
		}
	}

	// ---------------------------------------------------------------- missing definitions --

	// A function declared without a body, with the warning on it and where its definition goes in its source file.
	struct Undefined
	{
		const MissingDefinition* missing = nullptr;
		FixedDiagnostic warning;
		std::optional<DefinitionSite> site;
		WrittenDefinition written; // as it reads at the site; no text when it cannot be named from there
	};

	static CodeActionEdit InsertionAt(const DefinitionSite& site, const std::vector<WrittenDefinition>& definitions)
	{
		CodeActionEdit edit;
		edit.uri = site.uri;
		edit.range = Protocol::Range{ site.at, site.at };
		edit.newText = DefinitionSites::Joined(site, definitions, edit.caret);
		return edit;
	}

	static CodeAction DefinitionFix(const std::string& title, const FixedDiagnostic& warning)
	{
		CodeAction action;
		action.title = title;
		action.kind = "quickfix";
		action.diagnostics.Push(warning);
		return action;
	}

	static CodeActionEdit InPlaceEdit(Server::CompilationSession& session, uint32 sourceId, const std::string& uri, const MissingDefinition& missing)
	{
		const C4Compiler::Token& semicolon = missing.declaration->GetSemicolonToken();
		const WrittenDefinition body = DefinitionWriter::InPlaceBody(session, missing, DefinitionSites::InPlaceStyle(session, *missing.declaration));
		CodeActionEdit edit;
		edit.uri = uri;
		edit.range = session.ToEditorRange(sourceId, semicolon.GetStartLocation(), semicolon.GetEndLocation());
		edit.newText = body.text;
		edit.caret = body.caret;
		return edit;
	}

	// Several missing in one file: one action defines them all there, each where its own action puts it, those put at one
	// point in the order they are declared. The cursor lands in the first.
	static void AddAllInFileActions(const std::vector<Undefined>& undefined, Core::Vector<CodeAction>& out)
	{
		std::map<uint32, std::vector<const Undefined*>> byFile;
		for (const Undefined& u : undefined)
			if (u.site && !u.written.text.empty())
				byFile[u.site->sourceId].push_back(&u);
		for (const auto& [file, group] : byFile)
		{
			if (group.size() < 2)
				continue;
			CodeAction all;
			all.title = "Create the " + std::to_string(group.size()) + " missing definitions in " + DefinitionSites::FileName(group[0]->site->uri);
			all.kind = "quickfix";
			std::vector<std::vector<const Undefined*>> points;
			for (const Undefined* u : group)
			{
				all.diagnostics.Push(u->warning);
				auto same = std::find_if(points.begin(), points.end(), [&](const std::vector<const Undefined*>& point) {
					return point[0]->site->at.line == u->site->at.line && point[0]->site->at.character == u->site->at.character;
				});
				if (same == points.end())
					points.push_back({ u });
				else
					same->push_back(u);
			}
			for (const auto& together : points)
			{
				std::vector<WrittenDefinition> written;
				for (const Undefined* u : together)
					written.push_back(u->written);
				CodeActionEdit edit = InsertionAt(*together[0]->site, written);
				if (all.edits.Count() > 0)
					edit.caret = CodeActionEdit::c_NoCaret;
				all.edits.Push(std::move(edit));
			}
			out.Push(std::move(all));
		}
	}

	// For each function the document declares that nothing defines: its definition in its source file (preferred), in
	// place of its `;`, and all of them at once.
	static void AddMissingDefinitionActions(Server::CompilationSession& session, uint32 sourceId, const std::string& uri, Core::Vector<CodeAction>& out)
	{
		const Core::Vector<MissingDefinition> missing = MissingDefinitions::InDocument(session, sourceId);
		std::vector<Undefined> undefined;
		for (const MissingDefinition& m : missing)
		{
			Undefined u;
			u.missing = &m;
			u.warning = FixedDiagnostic{ MissingDefinitions::NameRange(session, *m.declaration),
				static_cast<int>(Protocol::DiagnosticSeverity::Warning), MissingDefinitions::MessageFor(*m.declaration) };
			u.site = DefinitionSites::InSourceFile(session, *m.declaration);
			if (u.site)
				u.written = DefinitionWriter::OutOfLine(session, m, u.site->namespaces, u.site->style);
			undefined.push_back(std::move(u));
		}

		for (const Undefined& u : undefined)
		{
			const std::string name = DeclarationText::NameOf(*u.missing->declaration);
			if (u.site && !u.written.text.empty())
			{
				CodeAction inFile = DefinitionFix("Create definition of '" + name + "' in " + DefinitionSites::FileName(u.site->uri), u.warning);
				inFile.preferred = true;
				inFile.edits.Push(InsertionAt(*u.site, { u.written }));
				out.Push(std::move(inFile));
			}
			CodeAction inPlace = DefinitionFix("Create definition of '" + name + "' in place", u.warning);
			inPlace.edits.Push(InPlaceEdit(session, sourceId, uri, *u.missing));
			out.Push(std::move(inPlace));
		}
		AddAllInFileActions(undefined, out);
	}

	// ------------------------------------------------------------------ entry --

	Core::Vector<CodeAction> CodeActions::ForDocument(Server::CompilationSession& session, const std::string& uri)
	{
		Core::Vector<CodeAction> result;
		if (!s_QuickfixesEnabled && !s_CreateDefinitionEnabled)
			return result; // both off: behave exactly as if this feature never existed

		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0) return result;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return result; // no parse tree: nothing to anchor edits on (same policy as hover/definition)

		const auto& rootAst = unit->GetRootAST();
		// This file's LSP diagnostics, token-span-widened and include-deduped by the shared pipeline.
		Core::Vector<Protocol::Diagnostic> docDiags = Diagnostics::ForDocument(session, uri);

		if (s_QuickfixesEnabled)
		{
			for (const auto& diag : docDiags)
				if (IsPointerDotMessage(diag.message))
					AddPointerQuickfix(session, sourceId, uri, diag, result);
		}

		if (s_CreateDefinitionEnabled)
		{
			std::string editorText = session.GetEditorText(uri);
			AddCreateDefinitionActions(session, sourceId, uri, rootAst, editorText, docDiags, result);
			if (MissingDefinitions::IsEnabled()) // the fixes of its warning: none without the squiggle they belong to
				AddMissingDefinitionActions(session, sourceId, uri, result);
		}

		return result;
	}
} // namespace C4LSP::Features
