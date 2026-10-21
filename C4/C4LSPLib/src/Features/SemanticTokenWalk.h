#pragma once
// The semantic-token classifier's state and steps, shared by SemanticTokens.cpp (the lexical pass and the result),
// SemanticTokenClassify.cpp (what a declaration or reference is) and SemanticTokenWalk.cpp (the AST walk).
#include "Features/SemanticTokens.h"
#include "Server/SemanticResolver.h"
#include "Lexer/Token.h"
#include "Model/ProgramModel.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h" // ObjectKind
#include "Parser/AstNode.h"
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Scripting
{
	class QualTypeNode;
}

namespace C4LSP::Features::TokenWalk
{
	/// The kind + modifiers to attach to a single classified token.
	struct TokInfo
	{
		uint32 kind = 0;       // SemanticTokenKind id
		uint32 modifiers = None;
	};

	constexpr uint32 K(SemanticTokenKind k) { return static_cast<uint32>(k); }

	/// Everything the classifier needs while walking the AST and emitting tokens.
	struct Ctx
	{
		const Server::SemanticResolver* resolver = nullptr; // the compiler's own query verbs (source of truth)
		uint32 sourceId = 0; // only this file's own nodes are recorded by offset
		// Token start (1-based line, column) -> info. Keyed on LINE/COLUMN rather than absolute byte
		// offset: the AST was parsed from the EFFECTIVE text (MacroPass normalizes CRLF->LF and blanks
		// out preprocessor directive lines), whose byte offsets drift from the editor buffer's as soon
		// as a file contains any #define/#if. Line/column survives that transformation for every line
		// MacroPass preserves verbatim, which is exactly the set of lines where declarations live; a
		// miss on an inline-expanded line degrades to the byName fallback instead of landing on a
		// neighboring token's kind (the old byte-keyed collision).
		std::map<std::pair<uint32, uint32>, TokInfo> byOffset; // (line, column) -> info (exact)
		std::unordered_map<std::string, TokInfo> byName; // declaration name -> info (fallback)
		std::unordered_set<const Scripting::AstNode*> fields; // variable decls that are object members

		/// Simple declared TYPE names ("Test", "Point") mapped to their own token kind. Populated as the walk
		/// meets each ObjectDefinition/EnumDefinition, so a chain segment or bare reference whose qualified path
		/// doesn't resolve through the resolver still reads with its real declaration kind (class gold) instead
		/// of the conservative namespace fallback that used to paint `Test` in `Test::DoThing()` as a namespace.
		std::unordered_map<std::string, uint32> typeNames;

		/// Every namespace path in the compiled program, outermost-first and joined with "::"
		/// ("Win", "Win::Test"), plus each segment prefix. Built from the model's scope tree via the
		/// resolver (a NamespaceScope is created per `namespace X` declaration). Used to classify the
		/// namespace segments of qualified chains (`Win::Test::Func`) without re-running analysis.
		std::unordered_set<std::string> namespacePaths;

		/// Names declared as template parameters (`template<T, uint32 I>`), collected during the AST
		/// walk so references to them classify as typeParameter instead of variable. Filled by the
		/// TemplateDefinition case before its children are walked (declaration-before-use holds for
		/// C4 templates: `template<...> class X` parses the parameter list first).
		std::unordered_set<std::string> templateParamNames;
	};

	uint32 ObjectKindToToken(Scripting::ObjectKind kind);
	const Scripting::ObjectType* AsObject(const Scripting::SymbolInfo* info);
	uint32 DeclKind(const Scripting::AstNode* decl, bool isMemberAccess, const Ctx& ctx);
	bool IsFunctionType(const Scripting::Type* type);
	uint32 ClassifyReference(const Ctx& ctx, const Scripting::AstNode& refNode, const Core::Vector<const Scripting::AstNode*>& path, bool isMemberAccess);
	void RecordOffset(Ctx& ctx, const Scripting::SourceLocation& loc, TokInfo info);
	void RecordName(Ctx& ctx, const Scripting::SourceLocation& loc, const std::string& name, TokInfo info);
	void BuildNamespacePaths(Ctx& ctx);
	bool IsNamespacePrefix(const Ctx& ctx, const std::string& name);
	uint32 ClassifyChainSegment(const Ctx& ctx, const std::vector<std::string>& segments, size_t i);
	void ClassifyTypeSegments(const Scripting::QualTypeNode& qt, Ctx& ctx, const Core::Vector<const Scripting::AstNode*>& path);
	void WalkTemplateArgument(const Scripting::AstNode& arg, Ctx& ctx, const Core::Vector<const Scripting::AstNode*>& path);

	/// True when `node` (the last entry of `path`) is what its parent call calls: `x.Get` in `x.Get()`.
	bool IsCallee(const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& path);
	void Walk(const Scripting::AstNode& node, Ctx& ctx, const Core::Vector<const Scripting::AstNode*>& pathIn, int objectDepth, bool directMember);
} // namespace C4LSP::Features::TokenWalk
