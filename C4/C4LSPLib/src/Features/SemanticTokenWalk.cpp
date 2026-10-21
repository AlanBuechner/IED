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
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/SwizzleViewNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace C4LSP::Features::TokenWalk
{
	/// Classify a template-argument node (`Box<int32>`, `Pair<T, Vec2>`): each argument is parsed as its own
	/// QualType / literal by the parser, so types inside `<...>` must be colored with their OWN declaration kind -
	/// exactly like the surrounding type context. Without this the arguments fell through to plain identifier
	/// classification and every `int32` in a template argument read as an unresolved VARIABLE (gold), while the
	/// same token in `Box<int32> origin;` or `int32 id = 5;` read blue: one token, three colors. First-wins in
	/// RecordOffset keeps this from clobbering a classification another node already recorded for the same offset.
	void WalkTemplateArgument(const Scripting::AstNode& arg, Ctx& ctx, const Core::Vector<const Scripting::AstNode*>& path)
	{
		if (arg.Type() == Scripting::NodeType::QualType)
		{
			ClassifyTypeSegments(static_cast<const Scripting::QualTypeNode&>(arg), ctx, path);
		}
		else if (arg.Type() == Scripting::NodeType::TemplateArgumentList)
		{
			const auto& list = static_cast<const Scripting::TemplateArgumentListNode&>(arg); // nested `Box<Pair<int32>>`
			for (uint32 i = 0; i < list.ArgumentCount(); ++i)
				if (list.GetArguments()[i])
					WalkTemplateArgument(*list.GetArguments()[i], ctx, path);
		}

		// Non-type arguments (literals, identifiers) fall through: the generic child recursion below reaches them
		// and classifies them exactly like any other expression in the same file.
	}

	/// True when `node` (the last entry of `path`) is what its parent call calls: `x.Get` in `x.Get()`.
	bool IsCallee(const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& path)
	{
		if (path.Count() < 2 || path[path.Count() - 2]->Type() != Scripting::NodeType::FunctionCall)
			return false;
		return static_cast<const Scripting::FunctionCallNode*>(path[path.Count() - 2])->GetCallee().Get() == &node;
	}

	// --- AST walk -----------------------------------------------------------
	void Walk(const Scripting::AstNode& node, Ctx& ctx, const Core::Vector<const Scripting::AstNode*>& pathIn, int objectDepth, bool directMember)
	{
		Core::Vector<const Scripting::AstNode*> path = pathIn;
		path.Push(&node);

		switch (node.Type())
		{
		case Scripting::NodeType::Namespace: {
			const auto& ns = static_cast<const Scripting::NamespaceNode&>(node);
			if (!ns.IsRoot() && !ns.Name().empty())
			{
				TokInfo info{ K(SemanticTokenKind::Namespace), SemanticTokenModifiers::Definition };
				RecordOffset(ctx, ns.GetNameToken().GetStartLocation(), info);
				RecordName(ctx, ns.GetNameToken().GetStartLocation(), ns.Name(), info);
			}
			break;
		}
		case Scripting::NodeType::ObjectDefinition: {
			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(node);
			TokInfo info{ ObjectKindToToken(obj.Kind()), SemanticTokenModifiers::Definition };
			RecordOffset(ctx, obj.GetNameToken().GetStartLocation(), info);
			RecordName(ctx, obj.GetNameToken().GetStartLocation(), obj.GetNameToken().GetText(), info);
			if (ctx.typeNames.count(obj.GetNameToken().GetText()) == 0) // first declaration wins
				ctx.typeNames.emplace(obj.GetNameToken().GetText(), info.kind);
			break;
		}
		case Scripting::NodeType::EnumDefinition: {
			const auto& en = static_cast<const Scripting::EnumDefinitionNode&>(node);
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
		case Scripting::NodeType::UnionDefinition: {
			const auto& un = static_cast<const Scripting::UnionDefinitionNode&>(node);
			TokInfo info{ K(SemanticTokenKind::Type), SemanticTokenModifiers::Definition }; // no `union` kind in the legend
			RecordOffset(ctx, un.GetNameToken().GetStartLocation(), info);
			RecordName(ctx, un.GetNameToken().GetStartLocation(), un.GetNameToken().GetText(), info);
			break;
		}
		case Scripting::NodeType::SwizzleView: {
			const auto& sv = static_cast<const Scripting::SwizzleViewNode&>(node);
			// `packed` / `unpacked` is lexed as an identifier (the parser commits it by text), so no AST name node
			// exists for it and the re-lex pass would otherwise fall through to variable. The keyword token is the
			// view's first token: record its exact span with kind Keyword, like `const` / `static`.
			const Scripting::SourceLocation kwLoc = sv.startLocation;
			if (kwLoc.line > 0)
				RecordOffset(ctx, kwLoc, TokInfo{ K(SemanticTokenKind::Keyword), None });

			// Lane names are stored as raw tokens (not VariableDefinition nodes): color them like field
			// definitions so `v.x` reads at the same family as a plain struct member.
			for (const auto& lane : sv.GetLanes())
				if (lane.GetType() == Scripting::TokenType::Identifier)
					RecordOffset(ctx, lane.GetStartLocation(), TokInfo{ K(SemanticTokenKind::Field), SemanticTokenModifiers::Definition });

			// SwizzleViewNode reports NO children, so the element type (`packed float x, y;`) is never reached by
			// the generic recursion. Walk it explicitly: `float` then gets its real QualType classification
			// (primitive -> keyword family + builtin) instead of degrading to an unresolved variable at re-lex time.
			if (sv.GetElementType())
				Walk(*sv.GetElementType().Get(), ctx, path, objectDepth, directMember);
			break; // the element type was just walked explicitly; there are no other children
		}
		case Scripting::NodeType::AliasDefinition: {
			const auto& al = static_cast<const Scripting::AliasDefinitionNode&>(node);
			TokInfo info{ K(SemanticTokenKind::Type), SemanticTokenModifiers::Definition };
			RecordOffset(ctx, al.GetNameToken().GetStartLocation(), info);
			RecordName(ctx, al.GetNameToken().GetStartLocation(), al.GetNameToken().GetText(), info);
			break;
		}
		case Scripting::NodeType::TemplateDefinition: {
			const auto& tpl = static_cast<const Scripting::TemplateDefinitionNode&>(node);
			for (uint32 pi = 0; pi < tpl.ParameterCount(); ++pi)
			{
				auto param = tpl.GetParameter(pi).As<Scripting::TemplateParameterNode>();
				if (!param || !param->GetNameToken().GetText().size())
					continue;

				const Scripting::Token& nameTok = param->GetNameToken();
				TokInfo info{ K(SemanticTokenKind::TypeParameter), None };
				RecordOffset(ctx, nameTok.GetStartLocation(), info);
				ctx.templateParamNames.insert(nameTok.GetText()); // references to T resolve through this

				// The `typename` / `class` marker token (when spelled as an identifier) is not a keyword;
				// it reads like one. Non-type parameters keep their type specifier colored by the QualType walk.
				const Scripting::Token& kw = param->GetKeywordToken();
				if (kw.GetType() == Scripting::TokenType::Identifier &&
				    (kw.GetText() == "typename" || kw.GetText() == "class"))
					RecordOffset(ctx, kw.GetStartLocation(), TokInfo{ K(SemanticTokenKind::Keyword), None });
			}
			break;
		}
		case Scripting::NodeType::FunctionDefinition: {
			const auto& fn = static_cast<const Scripting::FunctionDefinitionNode&>(node);
			if (fn.GetName())
			{
				const auto& qn = *fn.GetName();
				uint32 kind = objectDepth > 0 ? K(SemanticTokenKind::Method) : K(SemanticTokenKind::Function);

				if (qn.SegmentCount() <= 1)
				{
					// Plain function/method name: the whole token is the definition.
					TokInfo info{ kind, SemanticTokenModifiers::Definition };
					const Scripting::Token& nameTok = qn.GetFirstIdentifierToken();
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
							const Scripting::SymbolInfo* owner = ctx.resolver->Lookup(ownerPrefix, 0);
							if (AsObject(owner))
								kind = K(SemanticTokenKind::Method);
						}
					}

					TokInfo info{ kind, SemanticTokenModifiers::Definition };
					const Scripting::Token& nameTok = qn.GetSegments()[qn.SegmentCount() - 1].nameToken;
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
		case Scripting::NodeType::VariableDefinition: {
			const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(node);
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
		case Scripting::NodeType::Parameter: {
			const auto& param = static_cast<const Scripting::ParameterNode&>(node);
			if (param.HasName())
			{
				TokInfo info{ K(SemanticTokenKind::Variable), None }; // house style: parameters are not a distinct color from variables
				RecordOffset(ctx, param.GetNameToken().GetStartLocation(), info);
			}
			break;
		}
		case Scripting::NodeType::QualType: {
			ClassifyTypeSegments(static_cast<const Scripting::QualTypeNode&>(node), ctx, path);
			break;
		}
		case Scripting::NodeType::AnnotationItem: {
			const auto& item = static_cast<const Scripting::AnnotationItem&>(node);
			for (const auto& nameTok : item.GetNameTokens())
				RecordOffset(ctx, nameTok.GetStartLocation(), TokInfo{ K(SemanticTokenKind::Macro), None });
			break;
		}
		case Scripting::NodeType::IdentifierExpression: {
			const auto& id = static_cast<const Scripting::IdentifierExpressionNode&>(node);
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
		case Scripting::NodeType::MemberAccess: {
			const auto& ma = static_cast<const Scripting::MemberAccessNode&>(node);
			uint32 kind = ClassifyReference(ctx, node, path, /*isMemberAccess=*/true);
			// A member the resolver could not place (its owner is itself a member, `o.inner.Get()`) is still a method
			// when it is called; a resolved declaration keeps its own kind (a called function-pointer field stays a field).
			const bool declared = ctx.resolver && ctx.resolver->ResolveReference(node, path).declaration;
			if (!declared && IsCallee(node, path))
				kind = K(SemanticTokenKind::Method);
			RecordOffset(ctx, ma.GetMemberNameToken().GetStartLocation(), TokInfo{ kind, None });
			break;
		}
		case Scripting::NodeType::StaticAccess: {
			const auto& sa = static_cast<const Scripting::StaticAccessNode&>(node);

			// `A::B::C` nests as StaticAccess(StaticAccess(A, B), C): descend to the base collecting member names.
			std::vector<std::string> revSegments; // innermost member first
			std::vector<Scripting::Token> revTokens;
			const Scripting::AstNode* cur = &node;
			while (cur && cur->Type() == Scripting::NodeType::StaticAccess)
			{
				const auto& inner = static_cast<const Scripting::StaticAccessNode&>(*cur);
				revSegments.push_back(std::string(inner.GetMemberNameToken().GetText()));
				revTokens.push_back(inner.GetMemberNameToken());
				cur = inner.GetBaseType().Get();
			}

			bool ok = false; // the chain must terminate in a plain identifier to be classifiable per segment
			if (cur && cur->Type() == Scripting::NodeType::IdentifierExpression)
			{
				const auto& id = static_cast<const Scripting::IdentifierExpressionNode&>(*cur);
				if (id.GetNameToken().GetType() == Scripting::TokenType::Identifier)
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

		// `Box<int32>` / `Identity<T>(41)`: the parser stores template arguments as their own nodes that NO generic
		// child walk reaches (NamespaceQualifiedNameNode has no Children(), FunctionCallNode::Children() excludes them).
		// Walk them explicitly so type arguments color with their declaration kind instead of falling to variable.
		case Scripting::NodeType::TemplateArgumentList: {
			const auto& list = static_cast<const Scripting::TemplateArgumentListNode&>(node);
			for (uint32 i = 0; i < list.ArgumentCount(); ++i)
				if (list.GetArguments()[i])
					WalkTemplateArgument(*list.GetArguments()[i], ctx, path);
			break; // arguments are NOT generic children: they were just walked with template-argument semantics
		}
		case Scripting::NodeType::FunctionCall: {
			const auto& call = static_cast<const Scripting::FunctionCallNode&>(node);
			if (call.HasTemplateArgs())
				WalkTemplateArgument(*call.GetTemplateArgs(), ctx, path); // callee + args still recurse below
			break;
		}

		default:
			break;
		}

		// Recurse into children. Body members of an object are direct members (fields / methods);
		// everything nested inside a function body resets to non-member so locals stay `variable`.
		auto children = node.Children();
		bool childDirectMember = directMember;
		if (node.Type() == Scripting::NodeType::ObjectDefinition)
			childDirectMember = true;
		else if (node.Type() == Scripting::NodeType::FunctionDefinition ||
		         node.Type() == Scripting::NodeType::VariableDefinition)
			childDirectMember = false; // inside a body / initializer: not object members

		int childDepth = objectDepth;
		if (node.Type() == Scripting::NodeType::ObjectDefinition)
			childDepth = objectDepth + 1;

		for (const auto& child : children)
		{
			if (!child)
				continue;
			Walk(*child.Get(), ctx, path, childDepth, childDirectMember);
		}
	}
} // namespace C4LSP::Features::TokenWalk
