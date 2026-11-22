#include "Features/SemanticTokenWalk.h"
#include "Server/SemanticResolver.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Model/TypeHandle.h"    // TypeHandle::UnwrapToValue (alias/reference -> value type)
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/AnnotationDeclarationNode.h"
#include "Parser/Nodes/Declarations/ExternDeclarationNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/SwizzleViewNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"
#include "Parser/Nodes/Declarations/IncludeDeclarationNode.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/CompilationUnit.h"
#include "Server/WrittenChildren.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace C4LSP::Features::TokenWalk
{
	/// True when `node` (the last entry of `path`) is what its parent call calls: `x.Get` in `x.Get()`.
	bool IsCallee(const C4Compiler::AstNode& node, const Core::Vector<const C4Compiler::AstNode*>& path)
	{
		if (path.Count() < 2 || path[path.Count() - 2]->Type() != C4Compiler::NodeType::FunctionCall)
			return false;
		return static_cast<const C4Compiler::FunctionCallNode*>(path[path.Count() - 2])->GetCallee().Get() == &node;
	}

	/// A modifier the parser commits by its text (`inline`, `final`, `volatile`, ...): the lexer gives it as an identifier,
	/// so it reads as a keyword only when its node records it. A modifier the declaration does not have is an unset token,
	/// which RecordOffset skips.
	void RecordContextualKeyword(Ctx& ctx, const C4Compiler::Token& token)
	{
		RecordOffset(ctx, token.GetStartLocation(), TokInfo{ K(SemanticTokenKind::Keyword), None });
	}

	// --- AST walk -----------------------------------------------------------
	void Walk(const C4Compiler::AstNode& node, Ctx& ctx, const Core::Vector<const C4Compiler::AstNode*>& pathIn, int objectDepth, bool directMember)
	{
		Core::Vector<const C4Compiler::AstNode*> path = pathIn;
		path.Push(&node);

		switch (node.Type())
		{
		case C4Compiler::NodeType::Namespace: {
			const auto& ns = static_cast<const C4Compiler::NamespaceNode&>(node);
			if (!ns.IsRoot() && !ns.Name().empty())
			{
				TokInfo info{ K(SemanticTokenKind::Namespace), SemanticTokenModifiers::Definition };
				RecordOffset(ctx, ns.GetNameToken().GetStartLocation(), info);
				RecordName(ctx, ns.GetNameToken().GetStartLocation(), ns.Name(), info);
			}
			break;
		}
		case C4Compiler::NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const C4Compiler::ObjectDefinitionNode&>(node);
			RecordContextualKeyword(ctx, obj.GetAbstractToken());
			RecordContextualKeyword(ctx, obj.GetFinalToken());
			TokInfo info{ ObjectKindToToken(obj.Kind()), SemanticTokenModifiers::Definition };
			RecordOffset(ctx, obj.GetNameToken().GetStartLocation(), info);
			RecordName(ctx, obj.GetNameToken().GetStartLocation(), obj.GetNameToken().GetText(), info);
			if (ctx.typeNames.count(obj.GetNameToken().GetText()) == 0) // first declaration wins
				ctx.typeNames.emplace(obj.GetNameToken().GetText(), info.kind);
			break;
		}
		case C4Compiler::NodeType::EnumDefinition: {
			const auto& en = static_cast<const C4Compiler::EnumDefinitionNode&>(node);
			TokInfo info{ K(SemanticTokenKind::Enum), SemanticTokenModifiers::Definition };
			RecordOffset(ctx, en.GetNameToken().GetStartLocation(), info);
			RecordName(ctx, en.GetNameToken().GetStartLocation(), en.GetNameToken().GetText(), info);
			if (ctx.typeNames.count(en.GetNameToken().GetText()) == 0) // first declaration wins
				ctx.typeNames.emplace(en.GetNameToken().GetText(), info.kind);
			for (const auto& member : en.GetMembers())
			{
				if (!member)
					continue;
				TokInfo m{ K(SemanticTokenKind::Variable), SemanticTokenModifiers::Definition }; // enum constant
				RecordOffset(ctx, member->GetNameToken().GetStartLocation(), m);
				RecordName(ctx, member->GetNameToken().GetStartLocation(), member->GetNameToken().GetText(), m);
			}
			break;
		}
		case C4Compiler::NodeType::UnionDefinition: {
			const auto& un = static_cast<const C4Compiler::UnionDefinitionNode&>(node);
			TokInfo info{ K(SemanticTokenKind::Type), SemanticTokenModifiers::Definition }; // no `union` kind in the legend
			RecordOffset(ctx, un.GetNameToken().GetStartLocation(), info);
			RecordName(ctx, un.GetNameToken().GetStartLocation(), un.GetNameToken().GetText(), info);
			break;
		}
		case C4Compiler::NodeType::SwizzleView: {
			const auto& sv = static_cast<const C4Compiler::SwizzleViewNode&>(node);
			// `packed` / `unpacked` is lexed as an identifier (the parser commits it by text), so no AST name node
			// exists for it and the re-lex pass would otherwise fall through to variable. The keyword token is the
			// view's first token: record its exact span with kind Keyword, like `const` / `static`.
			const C4Compiler::SourceLocation kwLoc = sv.startLocation;
			if (kwLoc.line > 0)
				RecordOffset(ctx, kwLoc, TokInfo{ K(SemanticTokenKind::Keyword), None });

			// Lane names are stored as raw tokens (not VariableDefinition nodes): color them like field
			// definitions so `v.x` reads at the same family as a plain struct member.
			for (const auto& lane : sv.GetLanes())
				if (lane.GetType() == C4Compiler::TokenType::Identifier)
					RecordOffset(ctx, lane.GetStartLocation(), TokInfo{ K(SemanticTokenKind::Field), SemanticTokenModifiers::Definition });

			// SwizzleViewNode reports NO children, so the element type (`packed float x, y;`) is never reached by
			// the generic recursion. Walk it explicitly: `float` then gets its real QualType classification
			// (primitive -> keyword family + builtin) instead of degrading to an unresolved variable at re-lex time.
			if (sv.GetElementType())
				Walk(*sv.GetElementType().Get(), ctx, path, objectDepth, directMember);
			break; // the element type was just walked explicitly; there are no other children
		}
		case C4Compiler::NodeType::AliasDefinition: {
			const auto& al = static_cast<const C4Compiler::AliasDefinitionNode&>(node);
			TokInfo info{ K(SemanticTokenKind::Type), SemanticTokenModifiers::Definition };
			RecordOffset(ctx, al.GetNameToken().GetStartLocation(), info);
			RecordName(ctx, al.GetNameToken().GetStartLocation(), al.GetNameToken().GetText(), info);
			break;
		}
		case C4Compiler::NodeType::AnnotationDeclaration: {
			// `annotation`, `on` and the places are keywords only here; the name reads as its uses do.
			const auto& declaration = static_cast<const C4Compiler::AnnotationDeclarationNode&>(node);
			RecordContextualKeyword(ctx, declaration.GetKeywordToken());
			RecordOffset(ctx, declaration.GetNameToken().GetStartLocation(), TokInfo{ K(SemanticTokenKind::Macro), SemanticTokenModifiers::Definition });
			RecordContextualKeyword(ctx, declaration.GetOnToken());
			for (const C4Compiler::Token& place : declaration.GetPlaceTokens())
				RecordContextualKeyword(ctx, place);
			break;
		}
		case C4Compiler::NodeType::CastExpression:
			RecordContextualKeyword(ctx, static_cast<const C4Compiler::CastExpressionNode&>(node).GetCastKeywordToken());
			break;
		case C4Compiler::NodeType::TypeOfExpression:
			RecordContextualKeyword(ctx, static_cast<const C4Compiler::TypeOfExpressionNode&>(node).GetKeywordToken());
			break;
		case C4Compiler::NodeType::ExternDeclaration: {
			const auto& ext = static_cast<const C4Compiler::ExternDeclarationNode&>(node);
			RecordContextualKeyword(ctx, ext.GetKindToken());
			for (const C4Compiler::Token& directive : ext.GetDirectiveTokens())
				RecordContextualKeyword(ctx, directive);
			break;
		}
		case C4Compiler::NodeType::TemplateDefinition: {
			const auto& tpl = static_cast<const C4Compiler::TemplateDefinitionNode&>(node);
			for (uint32 pi = 0; pi < tpl.ParameterCount(); ++pi)
			{
				auto param = tpl.GetParameter(pi).As<C4Compiler::TemplateParameterNode>();
				if (!param || !param->GetNameToken().GetText().size())
					continue;

				const C4Compiler::Token& nameTok = param->GetNameToken();
				TokInfo info{ K(SemanticTokenKind::TypeParameter), None };
				RecordOffset(ctx, nameTok.GetStartLocation(), info);
				ctx.templateParamNames.insert(nameTok.GetText()); // references to T resolve through this

				// The `typename` / `class` marker token (when spelled as an identifier) is not a keyword;
				// it reads like one. Non-type parameters keep their type specifier colored by the QualType walk.
				const C4Compiler::Token& kw = param->GetKeywordToken();
				if (kw.GetType() == C4Compiler::TokenType::Identifier &&
				    (kw.GetText() == "typename" || kw.GetText() == "class"))
					RecordOffset(ctx, kw.GetStartLocation(), TokInfo{ K(SemanticTokenKind::Keyword), None });
			}
			break;
		}
		case C4Compiler::NodeType::FunctionDefinition: {
			const auto& fn = static_cast<const C4Compiler::FunctionDefinitionNode&>(node);
			for (const C4Compiler::Token* modifier : { &fn.GetInlineToken(), &fn.GetFinalToken(), &fn.GetAbstractToken(), &fn.GetNoexceptToken() })
				RecordContextualKeyword(ctx, *modifier);
			if (fn.GetName())
			{
				const auto& qn = *fn.GetName();
				uint32 kind = objectDepth > 0 ? K(SemanticTokenKind::Method) : K(SemanticTokenKind::Function);

				if (qn.SegmentCount() <= 1)
				{
					// Plain function/method name: the whole token is the definition.
					TokInfo info{ kind, SemanticTokenModifiers::Definition };
					const C4Compiler::Token& nameTok = qn.GetFirstIdentifierToken();
					RecordOffset(ctx, nameTok.GetStartLocation(), info);
					RecordName(ctx, nameTok.GetStartLocation(), nameTok.GetText(), info);
				}
				else
				{
					// Out-of-line qualified definition (`Win::Test::Func`): only the LAST segment is
					// the function; earlier segments are owner namespaces/types and must keep their own
					// kinds. Stamping every (or just the first) segment with method+definition poisons
					// the re-lex pass - `Win` would render as a function definition instead of namespace.
					std::vector<std::string> segs; // outermost first, matching ClassifyChainSegment's input
					for (uint32 si = 0; si < qn.SegmentCount(); ++si)
						segs.push_back(std::string(qn.GetSegments()[si].nameToken.GetText()));

					// method vs function is decided by the OWNER type: a member of an object definition
					// reads as a method even when defined out-of-line (objectDepth doesn't reach here).
					if (segs.size() > 1)
					{
						std::string ownerPrefix;
						for (size_t j = 0; j + 1 < segs.size(); ++j)
							ownerPrefix += (j ? "::" : "") + segs[j];
						if (ctx.resolver)
						{
							const C4Compiler::SymbolInfo* owner = ctx.resolver->Lookup(ownerPrefix, 0);
							if (AsObject(owner))
								kind = K(SemanticTokenKind::Method);
						}
					}

					TokInfo info{ kind, SemanticTokenModifiers::Definition };
					const C4Compiler::Token& nameTok = qn.GetSegments()[qn.SegmentCount() - 1].nameToken;
					RecordOffset(ctx, nameTok.GetStartLocation(), info);
					RecordName(ctx, nameTok.GetStartLocation(), nameTok.GetText(), info);

					for (size_t si = 0; si + 1 < segs.size(); ++si)
						// Owner segments: namespace prefixes and type prefixes get their declaration kinds.
						RecordOffset(ctx, qn.GetSegments()[si].nameToken.GetStartLocation(),
						             TokInfo{ ClassifyChainSegment(ctx, segs, si), None });
				}
			}
			break;
		}
		case C4Compiler::NodeType::VariableDefinition: {
			const auto& var = static_cast<const C4Compiler::VariableDefinitionNode&>(node);
			RecordContextualKeyword(ctx, var.GetVolatileToken());
			RecordContextualKeyword(ctx, var.GetThreadLocalToken());
			uint32 kind = directMember ? K(SemanticTokenKind::Field) : K(SemanticTokenKind::Variable);
			uint32 mods = SemanticTokenModifiers::Definition;
			if (var.IsStatic())
				mods |= SemanticTokenModifiers::Static;
			TokInfo info{ kind, mods };
			RecordOffset(ctx, var.GetNameToken().GetStartLocation(), info);
			RecordName(ctx, var.GetNameToken().GetStartLocation(), var.GetNameToken().GetText(), info);
			if (directMember)
				ctx.fields.insert(&node); // so bare references to this member also classify as field
			break;
		}
		case C4Compiler::NodeType::Parameter: {
			const auto& param = static_cast<const C4Compiler::ParameterNode&>(node);
			if (param.HasName())
			{
				TokInfo info{ K(SemanticTokenKind::Variable), None }; // house style: parameters are not a distinct color from variables
				RecordOffset(ctx, param.GetNameToken().GetStartLocation(), info);
			}
			break;
		}
		case C4Compiler::NodeType::QualType: {
			const auto& qt = static_cast<const C4Compiler::QualTypeNode&>(node);
			RecordContextualKeyword(ctx, qt.GetPackedToken());
			if (qt.IsTypename())
				RecordContextualKeyword(ctx, qt.BaseName()->GetFirstIdentifierToken()); // an annotation's parameter that takes a type
			else
				ClassifyTypeSegments(qt, ctx, path);
			break;
		}
		case C4Compiler::NodeType::AnnotationItem: {
			const auto& item = static_cast<const C4Compiler::AnnotationItem&>(node);
			for (const auto& nameTok : item.GetNameTokens())
				RecordOffset(ctx, nameTok.GetStartLocation(), TokInfo{ K(SemanticTokenKind::Macro), None });
			break;
		}
		case C4Compiler::NodeType::IdentifierExpression: {
			const auto& id = static_cast<const C4Compiler::IdentifierExpressionNode&>(node);
			const Server::NodeResolution res = ctx.resolver ? ctx.resolver->ResolveReference(node, path) : Server::NodeResolution{};
			// UNRESOLVED identifier (no type AND no declaration): do not record it. A recorded Variable would
			// otherwise shadow the macro-usage check at relex time - an undefined name that is a project #define
			// (or predefined macro) must read purple, and only the lexer loop can see those sets. Resolved
			// references keep recording exactly as before.
			if (!res.type && !res.declaration)
				break;
			uint32 kind = ClassifyReference(ctx, node, path, /*isMemberAccess=*/false);
			RecordOffset(ctx, id.GetNameToken().GetStartLocation(), TokInfo{ kind, None });
			break;
		}
		case C4Compiler::NodeType::MemberAccess: {
			const auto& ma = static_cast<const C4Compiler::MemberAccessNode&>(node);
			uint32 kind = ClassifyReference(ctx, node, path, /*isMemberAccess=*/true);
			// A member the resolver could not place (its owner is itself a member, `o.inner.Get()`) is still a method
			// when it is called; a resolved declaration keeps its own kind (a called function-pointer field stays a field).
			const bool declared = ctx.resolver && ctx.resolver->ResolveReference(node, path).declaration;
			if (!declared && IsCallee(node, path))
				kind = K(SemanticTokenKind::Method);
			RecordOffset(ctx, ma.GetMemberNameToken().GetStartLocation(), TokInfo{ kind, None });
			break;
		}
		case C4Compiler::NodeType::StaticAccess: {
			const auto& sa = static_cast<const C4Compiler::StaticAccessNode&>(node);

			// `A::B::C` nests as StaticAccess(StaticAccess(A, B), C): descend to the base collecting member names.
			std::vector<std::string> revSegments; // innermost member first
			std::vector<C4Compiler::Token> revTokens;
			const C4Compiler::AstNode* cur = &node;
			while (cur && cur->Type() == C4Compiler::NodeType::StaticAccess)
			{
				const auto& inner = static_cast<const C4Compiler::StaticAccessNode&>(*cur);
				revSegments.push_back(std::string(inner.GetMemberNameToken().GetText()));
				revTokens.push_back(inner.GetMemberNameToken());
				cur = inner.GetBaseType().Get();
			}

			bool ok = false; // the chain must terminate in a plain identifier to be classifiable per segment
			if (cur && cur->Type() == C4Compiler::NodeType::IdentifierExpression)
			{
				const auto& id = static_cast<const C4Compiler::IdentifierExpressionNode&>(*cur);
				if (id.GetNameToken().GetType() == C4Compiler::TokenType::Identifier)
				{
					revSegments.push_back(std::string(id.GetNameToken().GetText()));
					revTokens.push_back(id.GetNameToken());
					std::reverse(revSegments.begin(), revSegments.end()); // outermost first
					std::reverse(revTokens.begin(), revTokens.end());
					ok = true;
				}
			}

			if (ok)
			{
				for (size_t i = 0; i < revSegments.size(); ++i)
					RecordOffset(ctx, revTokens[i].GetStartLocation(), TokInfo{ ClassifyChainSegment(ctx, revSegments, i), None });
			}
			else
			{
				uint32 kind = ClassifyReference(ctx, node, path, /*isMemberAccess=*/true);
				RecordOffset(ctx, sa.GetMemberNameToken().GetStartLocation(), TokInfo{ kind, None });
			}
			break;
		}

		case C4Compiler::NodeType::LambdaExpression: {
			// A capture names a variable (`[&counter]`); an init capture's variable is walked as its declaration.
			for (const C4Compiler::LambdaCapture& capture : static_cast<const C4Compiler::LambdaNode&>(node).GetCaptures())
				if (capture.kind == C4Compiler::LambdaCapture::Kind::Variable)
					RecordOffset(ctx, capture.name.GetStartLocation(), TokInfo{ K(SemanticTokenKind::Variable), None });
			break;
		}

		default:
			break;
		}

		// Recurse into children. Body members of an object are direct members (fields / methods);
		// everything nested inside a function body resets to non-member so locals stay `variable`.
		bool childDirectMember = directMember;
		if (node.Type() == C4Compiler::NodeType::ObjectDefinition)
			childDirectMember = true;
		else if (node.Type() == C4Compiler::NodeType::FunctionDefinition ||
		         node.Type() == C4Compiler::NodeType::VariableDefinition)
			childDirectMember = false; // inside a body / initializer: not object members

		int childDepth = objectDepth;
		if (node.Type() == C4Compiler::NodeType::ObjectDefinition)
			childDepth = objectDepth + 1;

		Server::ForEachWrittenChild(node, [&](const C4Compiler::AstNode& child) { Walk(child, ctx, path, childDepth, childDirectMember); });
	}

	// --- What the included headers declare --------------------------------------------------------------------------------

	namespace
	{
		// A header's type names and its objects' fields, as Walk records them; function bodies and initializers declare
		// neither.
		void CollectDeclared(const C4Compiler::AstNode& node, Ctx& ctx, bool directMember)
		{
			switch (node.Type())
			{
			case C4Compiler::NodeType::ObjectDefinition: {
				const auto& obj = static_cast<const C4Compiler::ObjectDefinitionNode&>(node);
				ctx.typeNames.emplace(obj.GetNameToken().GetText(), ObjectKindToToken(obj.Kind())); // first declaration wins
				break;
			}
			case C4Compiler::NodeType::EnumDefinition:
				ctx.typeNames.emplace(static_cast<const C4Compiler::EnumDefinitionNode&>(node).GetNameToken().GetText(), K(SemanticTokenKind::Enum));
				break;
			case C4Compiler::NodeType::VariableDefinition:
				if (directMember)
					ctx.fields.insert(&node);
				return;
			case C4Compiler::NodeType::FunctionDefinition:
				return;
			default:
				break;
			}
			const bool childDirectMember = directMember || node.Type() == C4Compiler::NodeType::ObjectDefinition;
			Server::ForEachWrittenChild(node, [&](const C4Compiler::AstNode& child) { CollectDeclared(child, ctx, childDirectMember); });
		}

		// The headers `unitId` includes, each in the place of its include (as the compiler's include merge splices them);
		// the unit's own declarations too when `own`.
		void CollectIncludes(C4Compiler::CompilerInstance& compiler, uint32 unitId, bool own, std::unordered_set<uint32>& seen, Ctx& ctx)
		{
			const Ref<C4Compiler::CompilationUnit> unit = compiler.GetCompilationUnit(unitId);
			if (!unit)
				return;
			for (const auto& child : unit->GetOriginalChildren())
			{
				if (!child)
					continue;
				if (const auto include = child.PointerCast<C4Compiler::IncludeDeclarationNode>())
				{
					const uint32 header = compiler.GetSourceManager().ResolveIncludePath(include->FilePath(), unitId);
					if (header != 0 && seen.insert(header).second)
						CollectIncludes(compiler, header, true, seen, ctx);
				}
				else if (own)
					CollectDeclared(*child.Get(), ctx, false);
			}
		}
	}

	void CollectIncludedDeclarations(C4Compiler::CompilerInstance& compiler, Ctx& ctx)
	{
		std::unordered_set<uint32> seen{ ctx.sourceId };
		CollectIncludes(compiler, ctx.sourceId, false, seen, ctx);
	}
} // namespace C4LSP::Features::TokenWalk
