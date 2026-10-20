#include "Features/SemanticTokens.h"
#include "Features/SemanticTokenWalk.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)

// AST node headers for classification.
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/SwizzleViewNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"

#include <algorithm>
#include <cctype>
#include <functional>

#include <map>
#include <unordered_map>
#include <unordered_set>

namespace C4LSP::Features
{
	using namespace TokenWalk;

	// ---------------------------------------------------------------------------
	// Internal helpers
	// ---------------------------------------------------------------------------
	namespace
	{
		// --- Keyword classification for the lexical pass ------------------------
		bool IsKeywordToken(Scripting::TokenType t)
		{
			return Scripting::Token::IsKeyword(t);
		}

		uint32 KeywordKind(Scripting::TokenType t)
		{
			switch (t)
			{
			case Scripting::TokenType::If:
			case Scripting::TokenType::Else:
			case Scripting::TokenType::Switch:
			case Scripting::TokenType::Case:
			case Scripting::TokenType::Default:
			case Scripting::TokenType::Fallthrough:
			case Scripting::TokenType::For:
			case Scripting::TokenType::While:
			case Scripting::TokenType::Do:
			case Scripting::TokenType::Break:
			case Scripting::TokenType::Continue:
			case Scripting::TokenType::Return:
			case Scripting::TokenType::Defer:
			case Scripting::TokenType::Goto:
			case Scripting::TokenType::Try:
			case Scripting::TokenType::Catch:
			case Scripting::TokenType::Finally:
			case Scripting::TokenType::Throw:
				return K(SemanticTokenKind::ControlFlow); // control flow reads lighter purple, distinct from plain keywords

			// Access modifiers and qualifiers (public / private / protected / virtual / override / const / static)
			// are ordinary blue keywords in the house style; the purple modifier family is reserved for macros.
			default:
				return K(SemanticTokenKind::Keyword);
			}
		}

		/// True when (line, column-1) matches a directive line start from `starts`.
		bool IsDirectiveLineStart(const std::vector<std::pair<uint32, uint32>>& starts, const Scripting::SourceLocation& loc)
		{
			for (const auto& s : starts)
				if (s.first == loc.line && s.second == (loc.column > 0 ? loc.column - 1 : 0))
					return true;
			return false;
		}

		/// True when `tok` (an identifier spelled "include") starts a real include directive, using the same
		/// commit rule as IncludeDeclarationNode::TryParse/ParseBody: next token is a string literal ending in ".ch".
		bool IsIncludeDirective(const Scripting::Lexer& lexer)
		{
			const Scripting::Token& path = lexer.PeekCompound();
			if (path.GetType() != Scripting::TokenType::Text)
				return false;
			const std::string& text = path.GetText(); // Core::String is std::string; .ch check matches the parser's substr test
			return text.size() >= 3 && text.substr(text.size() - 3) == ".ch";
		}

		/// Scan `text` for preprocessor directive lines and return their (line, column-1) start positions: any line whose
		/// first non-whitespace character is '#'. The lexer swallows the '#' into that line's first identifier ("#define"
		/// lexes as one token), so no AST node exists for it - this scan lets the emit pass color those tokens as macros.
		std::vector<std::pair<uint32, uint32>> DirectiveLineStarts(const std::string& text)
		{
			std::vector<std::pair<uint32, uint32>> out; // (1-based line, 0-based column of '#')
			uint32 line = 1;
			size_t pos = 0;
			while (pos < text.size())
			{
				size_t eol = text.find('\n', pos);
				bool lastLine = (eol == std::string::npos);
				std::size_t lineEnd = lastLine ? text.size() : eol;

				size_t i = pos;
				while (i < lineEnd && (text[i] == ' ' || text[i] == '\t'))
					++i;
				if (i < lineEnd && text[i] == '#')
					out.push_back({ line, static_cast<uint32>(i - pos) });

				pos = lastLine ? text.size() : eol + 1;
				++line;
			}
			return out;
		}

		void Emit(C4LSP::Features::SemanticTokensResult& out, const Scripting::Token& tok, uint32 kind, uint32 modifiers,
		           const Scripting::Source* source)
		{
			const auto& s = tok.GetStartLocation();
			const auto& e = tok.GetEndLocation();
			SemanticToken st;
			st.startLine = (s.line > 0) ? s.line - 1 : 0;
			st.startCharacter = (s.column > 0) ? s.column - 1 : 0;

			// Length in UTF-16 code units. For a single-line token this is the byte span (C4 sources
			// are treated as bytes); for a multi-line token (a block comment) it is the sum of each
			// covered line's text, which matches how the client counts characters across newlines.
			if (e.line == s.line || e.line <= 0)
				st.length = (e.byteOffset >= s.byteOffset) ? static_cast<uint32>(e.byteOffset - s.byteOffset) : 0;
			else if (source)
			{
				uint32 total = 0;
				for (uint32 line = s.line; line <= e.line && line > 0; ++line)
					total += static_cast<uint32>(source->GetLineText(line).size());
				st.length = total;
			}
			else
				st.length = (e.byteOffset >= s.byteOffset) ? static_cast<uint32>(e.byteOffset - s.byteOffset) : 0;

			if (st.length == 0)
				return; // nothing to highlight (empty / EOF token)
			st.kindId = kind;
			st.modifiers = modifiers;
			out.tokens.push_back(st);
		}
	}

	// ---------------------------------------------------------------------------
	// Public API
	// ---------------------------------------------------------------------------
	SemanticTokensResult SemanticTokens::ForDocument(Server::CompilationSession& session, const std::string& uri)
	{
		SemanticTokensResult result;
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return result;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		const Server::SemanticResolver* resolver = &session.GetResolver();

		// Re-lex the text the editor shows (unsaved contents, not the compiler's effective
		// form): token positions must match what is on screen. When the compiler parsed a
		// different (macro-expanded / interop-bound) form, byte offsets won't line up and the
		// name-based fallback below keeps identifiers classified correctly anyway.
		std::string text = session.GetEditorText(uri);

		if (text.empty())
			return result;

		// 1) Walk the AST to classify every named token in this file.
		Ctx ctx;
		ctx.resolver = resolver;
		ctx.sourceId = sourceId;
		BuildNamespacePaths(ctx); // namespace prefixes for qualified-chain classification (Win::Test::...)
		if (unit && unit->GetRootAST())
		{
			Core::Vector<const Scripting::AstNode*> path;
			Walk(*unit->GetRootAST().Get(), ctx, path, /*objectDepth=*/0, /*directMember=*/false);
		}

		// 2) Re-lex the text and emit one token per semantic lexeme.
		auto source = Scripting::Source::FromMemory(text);
		Scripting::Lexer lexer(*source.Get(), sourceId);

		// Preprocessor directive lines (their '#' is swallowed into an identifier by the lexer, so the
		// first token of each such line must be colored a macro): positions + the defined-macro names.
		auto directiveStarts = DirectiveLineStarts(text);
		std::unordered_set<std::string> definedMacros; // every macro name visible to the project (below) + predefined (-D) macros
		for (const auto& d : session.GetDefines())
			definedMacros.insert(d.first);
		std::unordered_set<std::string> pendingMacroNames; // #define names in this document's CURRENT text, colored before recompile sees them

		// Every macro name visible to the project, so a USE of `MAX_HP` reads purple (macro) wherever it appears -
		// Visual Assist colors the NAME at both its definition and its usages. Sources are scanned as TEXT (not via
		// the compiler's effective/macro-expanded form): semantic tokens must describe what is on screen, and a
		// `#define` line exists verbatim in every file that defines one. Predefined (-D) macros join the same set.
		auto& sourceManager = session.GetSourceManager();
		for (uint32 sid = 1; sid <= sourceManager.Count(); ++sid)
		{
			const Ref<Scripting::Source>& srcRef = sourceManager.GetInfo(sid).source;
			if (!srcRef || srcRef->Size() == 0)
				continue; // in-memory or not yet loaded: no text to scan (the editor's own file is covered below anyway)
			std::string allText(srcRef->Data(), srcRef->Size()); // full raw text - #define lines exist verbatim in it either way
			size_t pos = 0;
			while ((pos = allText.find("#define", pos)) != std::string::npos)
			{
				pos += 7; // past "#define"
				while (pos < allText.size() && (allText[pos] == ' ' || allText[pos] == '\t'))
					++pos;
				if (pos < allText.size() && (std::isalpha((unsigned char)allText[pos]) || allText[pos] == '_'))
				{
					size_t end = pos;
					while (end < allText.size() && (std::isalnum((unsigned char)allText[end]) || allText[end] == '_'))
						++end;
					definedMacros.insert(allText.substr(pos, end - pos));
				}
			}
		}

		// The compiler's effective text lags the editor by one recompile: a `#define` typed seconds ago is not in any
		// source yet. Scan THIS document's current text for directive lines so their names read purple immediately.
		for (const auto& s : directiveStarts) // (1-based line, 0-based column of '#') -> byte offset below
		{
			size_t lineBegin = 0;
			uint32 l = 1;
			while (l < s.first) // lines before this one
			{
				size_t eol = text.find('\n', lineBegin);
				if (eol == std::string::npos)
					break; // malformed position: stop rather than run off the end of `text`
				lineBegin = eol + 1;
				++l;
			}
			size_t byteOffset = lineBegin + s.second;
			if (byteOffset >= text.size())
				continue;
			std::string line(text, byteOffset); // from the '#' to end-of-file; find() never crosses a newline for "#define NAME"
			auto it = line.find("#define");
			if (it != std::string::npos)
			{
				size_t p = it + 7;
				while (p < line.size() && (line[p] == ' ' || line[p] == '\t'))
					++p;
				if (p < line.size() && (std::isalpha((unsigned char)line[p]) || line[p] == '_'))
				{
					size_t end = p;
					while (end < line.size() && (std::isalnum((unsigned char)line[end]) || line[end] == '_') && line[end] != '\n')
						++end;
					pendingMacroNames.insert(line.substr(p, end - p));
				}
			}
		}

		while (true)
		{
			const Scripting::Token& tok = lexer.NextToken();
			if (tok.GetType() == Scripting::TokenType::EndOfFile)
				break;

			switch (tok.GetType())
			{
			case Scripting::TokenType::Identifier: {
				const auto& startLoc = tok.GetStartLocation();
				auto it = ctx.byOffset.find({ startLoc.line, startLoc.column });
				uint32 kind;
				uint32 modifiers = None;
				if (it != ctx.byOffset.end())
				{
					kind = it->second.kind;
					modifiers = it->second.modifiers;
				}
				else
				{
					auto byNameIt = ctx.byName.find(tok.GetText());
					if (byNameIt != ctx.byName.end())
						kind = byNameIt->second.kind;
					else if (IsDirectiveLineStart(directiveStarts, startLoc))
					{
						// The lexer swallows the '#' into this identifier ("#define" lexes as one token "#define"), so a
						// directive line's first token is an Identifier that no AST node ever records. Preprocessor KEYWORDS
						// (#define, #if, #endif, ...) stay in the neutral keyword family (blue) - Visual Assist style; only
						// macro NAMES are purple (the name following a `#define` lands in `definedMacros` via the scan above).
						kind = K(SemanticTokenKind::Keyword);
					}
					else if (ctx.templateParamNames.count(tok.GetText()))
						kind = K(SemanticTokenKind::TypeParameter); // template parameter use site the semantic pass missed
					else if (definedMacros.count(tok.GetText()) != 0 || pendingMacroNames.count(tok.GetText()) != 0)
						kind = K(SemanticTokenKind::Macro); // a macro name: purple at its definition AND at every usage site
					else if (tok.GetText() == "include" && IsIncludeDirective(lexer))
						// `include` is lexed as a plain identifier and committed in IncludeDeclarationNode::TryParse,
						// so the AST walk never records it. Replicate that commit rule to color the directive blue.
						kind = K(SemanticTokenKind::Keyword);
					else
						kind = K(SemanticTokenKind::Variable); // unresolved identifier
				}
				Emit(result, tok, kind, modifiers, source.Get());
				break;
			}
			case Scripting::TokenType::Number:
			case Scripting::TokenType::Char:
				Emit(result, tok, K(SemanticTokenKind::Number), None, source.Get());
				break;
			case Scripting::TokenType::Comment:
			case Scripting::TokenType::MultiComment:
				Emit(result, tok, K(SemanticTokenKind::Comment), None, source.Get());
				break;
			case Scripting::TokenType::Text:
				Emit(result, tok, K(SemanticTokenKind::String), None, source.Get());
				break;
			default:
				if (IsKeywordToken(tok.GetType()))
					Emit(result, tok, KeywordKind(tok.GetType()), None, source.Get());
				// Operators and punctuation are intentionally omitted from semantic tokens: they carry no
				// symbol meaning and the standard C++ legend has no kind for them.
				break;
			}
		}

		return result;
	}

	Core::Vector<Core::String> SemanticTokens::TokenTypeNames()
	{
		static const char* names[] = {
			"namespace", "type", "class", "enum", "interface", "struct", "typeParameter",
			"parameter", "variable", "field", "function", "method", "property", "macro",
			"keyword", "modifier", "comment", "number", "string",

			// C4 extension kinds (appended AFTER the standard legend so existing ids stay stable).
			"controlFlow"
		};
		static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(SemanticTokenKind::Count),
		              "TokenTypeNames must match SemanticTokenKind");
		Core::Vector<Core::String> out;
		for (const char* n : names)
			out.Push(Core::String(n));
		return out;
	}

	Core::Vector<Core::String> SemanticTokens::ModifierNames()
	{
		static const char* names[] = { "declaration", "definition", "static", "readonly", "deprecated", "builtin" };
		Core::Vector<Core::String> out;
		for (const char* n : names)
			out.Push(Core::String(n));
		return out;
	}
} // namespace C4LSP::Features
