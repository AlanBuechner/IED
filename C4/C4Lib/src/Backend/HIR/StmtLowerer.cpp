#include "Backend/HIR/StmtLowerer.h"
#include "Backend/HIR/AsmLowerer.h"
#include "Parser/Nodes/Statements/AsmStatementNode.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/CaseLabels.h"
#include "Backend/HIR/Reachability.h"
#include "Backend/HIR/DestructuringLowerer.h"
#include "Backend/HIR/ReadOnly.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/SignatureType.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/LoopLowerer.h"
#include "Backend/HIR/ExceptionLowerer.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/LoweringDriver.h" // LowerFunction (local function bodies lower into a nested HIRFunction)
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/SimpleStatementNodes.h"
#include "Parser/Nodes/Statements/ExpressionStatementNode.h"
#include "Parser/Nodes/Statements/IfStatementNode.h"
#include "Parser/Nodes/Statements/WhileLoopNode.h"
#include "Parser/Nodes/Statements/ForLoopNode.h"
#include "Parser/Nodes/Statements/SwitchStatementNode.h"
#include "Parser/Nodes/Statements/TryCatchNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Parser/Nodes/Expressions/TernaryExpressionNode.h"
#include "Model/TypeHandle.h"
#include "Model/Types/AliasType.h"
#include <algorithm>

namespace Scripting
{
	RegionScope::RegionScope(LoweringContext& ctx, const BlockNode* block, const char* name)
		: m_Ctx(ctx), m_Scope(ctx.queries().PushScope(Scope::Kind::Block, name))
	{
		ctx.jumps.EnterBlock(block);
	}

	RegionScope::~RegionScope()
	{
		m_Ctx.jumps.LeaveBlock();
	}

	QueryResult<void> StmtLowerer::LowerStmt(const AstNode* node, HIRNode* region)
	{
		if (!node) return QueryResult<void>::Fail(QueryError::NotAValue, "empty statement");
		BlockNode* block = dynamic_cast<BlockNode*>(region);
		if (!block) return QueryResult<void>::Fail(QueryError::NotAValue, "region is not a block");
		const LoweringAt at(*m_Ctx.out, node->startLocation);
		// The statement's own temporaries are collected apart from the enclosing statement's.
		return Lifecycle(m_Ctx).LowerStatement(block, [&] { return Dispatch(node, block); });
	}

	QueryResult<void> StmtLowerer::Dispatch(const AstNode* node, BlockNode* block)
	{
		switch (node->Type()) {
		case NodeType::CodeBlock:           return LowerNestedBlock(static_cast<const CodeBlockNode*>(node), block);
		case NodeType::VariableDefinition:  return DeclLowerer(m_Ctx).LowerLocalDecl(static_cast<const VariableDefinitionNode*>(node), block);
		case NodeType::Destructuring:       return DestructuringLowerer(m_Ctx).Lower(*static_cast<const DestructuringNode*>(node), block);
		case NodeType::ExpressionStatement: return LowerExprStmt(static_cast<const ExpressionStatementNode*>(node), block);
		case NodeType::ReturnStatement:     return LowerReturn(static_cast<const ReturnStatementNode*>(node), block);
		case NodeType::IfStatement:         return LowerIf(static_cast<const IfStatementNode*>(node), block);
		case NodeType::WhileLoop:           return LoopLowerer(m_Ctx, *this).LowerWhile(static_cast<const WhileLoopNode*>(node), block);
		case NodeType::DoWhileLoop:         return LoopLowerer(m_Ctx, *this).LowerDoWhile(static_cast<const DoWhileLoopNode*>(node), block);
		case NodeType::ForLoop:             return LoopLowerer(m_Ctx, *this).LowerFor(static_cast<const ForLoopNode*>(node), block);
		case NodeType::SwitchStatement:     return LowerSwitch(static_cast<const SwitchStatementNode*>(node), block);
		case NodeType::BreakStatement:      return LowerBreak(static_cast<const BreakStatementNode*>(node), block);
		case NodeType::ContinueStatement:   return LowerContinue(static_cast<const ContinueStatementNode*>(node), block);
		case NodeType::GotoStatement:       return LowerGoto(static_cast<const GotoStatementNode*>(node), block);
		case NodeType::LabelStatement:      return LowerLabel(static_cast<const LabelStatementNode*>(node), block);
		// `fallthrough;` is only meaningful as a switch case's statement (LowerSwitch handles it there).
		case NodeType::FallthroughStatement: return m_Ctx.Fail(node, QueryResult<void>::Fail(QueryError::NotAValue, "'fallthrough' is only valid inside a switch case"));
		case NodeType::ObjectDefinition:    return LowerLocalType(node);
		case NodeType::EnumDefinition:      return LowerLocalType(node);
		case NodeType::AliasDefinition:     return LowerLocalAlias(static_cast<const AliasDefinitionNode*>(node));
		case NodeType::FunctionDefinition:  return LowerLocalFunction(static_cast<const FunctionDefinitionNode*>(node));
		case NodeType::TryCatch:            return ExceptionLowerer(m_Ctx, *this).LowerTryCatch(static_cast<const TryCatchNode*>(node), block);
		case NodeType::ThrowStatement:      return ExceptionLowerer(m_Ctx, *this).LowerThrow(static_cast<const ThrowStatementNode*>(node), block);
		case NodeType::DeferStatement:      return ExceptionLowerer(m_Ctx, *this).LowerDefer(static_cast<const DeferStatementNode*>(node), block);
		case NodeType::AsmStatement:        return AsmLowerer(m_Ctx).Lower(static_cast<const AsmStatementNode*>(node), block);
		case NodeType::EmptyStatement:      return QueryResult<void>::Success();
		default:
			return QueryResult<void>::Fail(QueryError::Unsupported, "statement not yet lowered");
		}
	}

	QueryResult<void> StmtLowerer::LowerBlock(const CodeBlockNode* n, BlockNode* region, const char* scopeName)
	{
		RegionScope scope(m_Ctx, region, scopeName);
		return LowerStatements(n, region);
	}

	QueryResult<void> StmtLowerer::LowerFunctionBody(const CodeBlockNode* n, BlockNode* root, uint32 leading,
		const std::function<QueryResult<void>()>& between)
	{
		RegionScope scope(m_Ctx, root, "block");
		const uint32 count = n->StatementCount();
		const uint32 split = leading < count ? leading : count;
		if (auto r = LowerEach(n, 0, split, root); !r.Ok()) return r;
		if (between)
			if (auto r = between(); !r.Ok()) return r;
		return LowerEach(n, split, count, root);
	}

	QueryResult<void> StmtLowerer::LowerStatements(const CodeBlockNode* n, BlockNode* target)
	{
		return LowerEach(n, 0, n->StatementCount(), target);
	}

	namespace
	{
		// Whether `node` names one of `names`.
		bool Names(const AstNode* node, const std::vector<Core::String>& names)
		{
			if (!node || names.empty()) return false;
			if (const auto* id = NodeAs<IdentifierExpressionNode>(node))
				if (std::find(names.begin(), names.end(), id->GetName()) != names.end()) return true;
			for (const Ref<AstNode>& child : node->Children())
				if (Names(child.Get(), names)) return true;
			return false;
		}

		void AddDeclaredNames(const AstNode* statement, std::vector<Core::String>& names)
		{
			if (const auto* declaration = dynamic_cast<const VariableDefinitionNode*>(statement))
				for (const Declarator& d : declaration->GetDeclarators()) names.push_back(d.m_NameToken.GetText());
		}
	}

	QueryResult<void> StmtLowerer::LowerEach(const CodeBlockNode* n, uint32 from, uint32 to, BlockNode* target)
	{
		QueryResult<void> first = QueryResult<void>::Success();
		std::vector<Core::String> undeclared; // the locals of failed declarations: a use of one is not reported again
		for (uint32 i = from; i < to; ++i)
		{
			const AstNode* statement = n->GetStatements()[i].Get();
			if (Names(statement, undeclared))
			{
				AddDeclaredNames(statement, undeclared);
				continue;
			}
			auto r = m_Ctx.Fail(statement, LowerStmt(statement, target));
			if (r.Ok()) continue;
			if (first.Ok()) first = r;
			AddDeclaredNames(statement, undeclared);
			m_Ctx.ReportNextFailure();
		}
		if (!first.Ok()) m_Ctx.NoteReported();
		return first;
	}

	QueryResult<void> StmtLowerer::LowerBody(const CodeBlockNode* block, const AstNode* single, BlockNode* target)
	{
		if (block) return LowerStatements(block, target);
		if (single) return LowerStmt(single, target); // braceless: `if (c) stmt;`
		return QueryResult<void>::Success();
	}

	BlockNode* StmtLowerer::MakeBodyRegion()
	{
		return m_Ctx.out->NewNode<BlockNode>(nullptr);
	}

	QueryResult<void> StmtLowerer::LowerNestedBlock(const CodeBlockNode* n, HIRNode* region)
	{
		// A nested `{ ... }` is its own region and scope, appended to the parent as a statement.
		BlockNode* child = MakeBodyRegion();
		static_cast<BlockNode*>(region)->stmts.push_back(child);
		return LowerBlock(n, child);
	}

	QueryResult<void> StmtLowerer::LowerLocalType(const AstNode* decl)
	{
		// A struct/class/enum declared inside a body: concrete in the store, its name only in the current
		// layer (doc 06). No HIR node is emitted.
		auto r = m_Ctx.queries().RegisterLocalType(*decl);
		if (!r.Ok()) return QueryResult<void>::Fail(r.error, r.message);
		return QueryResult<void>::Success();
	}

	QueryResult<void> StmtLowerer::LowerLocalAlias(const AliasDefinitionNode* alias)
	{
		auto r = m_Ctx.queries().RegisterLocalAlias(*alias);
		if (!r.Ok()) return QueryResult<void>::Fail(r.error, r.message);
		return QueryResult<void>::Success();
	}

	QueryResult<void> StmtLowerer::LowerLocalFunction(const FunctionDefinitionNode* fn)
	{
		// A function declared inside a body: its record lives in the current layer, its body lowers into a
		// nested HIRFunction with its OWN scope chain (the parent's locals are not visible inside it).
		auto r = m_Ctx.queries().RegisterLocalFunction(*fn);
		if (!r.Ok()) return QueryResult<void>::Fail(r.error, r.message);

		HIRFunction child = LowerFunction(m_Ctx.model(), m_Ctx.errors(), r.value);
		if (child.complete)
			m_Ctx.out->nestedFunctions.push_back(std::make_unique<HIRFunction>(std::move(child)));
		return QueryResult<void>::Success(); // a body-lowering error is reported by the child; the name still resolves
	}

	QueryResult<void> StmtLowerer::LowerExprStmt(const ExpressionStatementNode* n, HIRNode* region)
	{
		ExprLowerer expr(m_Ctx);
		auto e = expr.ResolveExpr(n->GetExpression().Get());
		if (!e.Ok()) return QueryResult<void>::Fail(e.error, e.message);
		static_cast<BlockNode*>(region)->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(Lifecycle(m_Ctx).Discarded(e.value)));
		return QueryResult<void>::Success();
	}

	QueryResult<void> StmtLowerer::LowerReturn(const ReturnStatementNode* n, HIRNode* region)
	{
		if (m_Ctx.deferDepth > 0)
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "'return' is not allowed inside a defer or finally body"));
		const Type* retType = m_Ctx.fn->type ? m_Ctx.fn->type->GetReturnType() : nullptr;
		const bool returnsVoid = !retType || retType->GetKind() == TypeKind::Void;
		const HIRNode* value = nullptr;
		if (n->GetExpression())
		{
			auto v = LowerReturnValue(n, retType);
			if (!v.Ok()) return QueryResult<void>::Fail(v.error, v.message);
			value = v.value;
		}
		else if (!returnsVoid)
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "missing return value for non-void function"));

		auto& stmts = static_cast<BlockNode*>(region)->stmts;
		if (returnsVoid && value) stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(value)); // `return F();`: F runs, nothing returns
		stmts.push_back(m_Ctx.out->NewNode<ReturnNode>(returnsVoid ? nullptr : value));
		return QueryResult<void>::Success();
	}

	bool StmtLowerer::OwnedByThisFunction(const LValue* lv) const
	{
		if (!lv || lv->kind != LValue::Kind::Slot) return false;
		SlotClass cls = m_Ctx.out->GetSlot(lv->slot).cls;
		return cls == SlotClass::Local || cls == SlotClass::Param;
	}

	// The local or by-value parameter a location is part of (`v`, `v.x`, a base of `v`), or null when it lives
	// behind a pointer or reference, or in static storage.
	const LValue* StmtLowerer::OwningLocal(const LValue* lv) const
	{
		while (lv && (lv->kind == LValue::Kind::Field || lv->kind == LValue::Kind::Base)) lv = lv->base;
		return OwnedByThisFunction(lv) ? lv : nullptr;
	}

	QueryResult<const HIRNode*> StmtLowerer::ReturnReference(const ReturnStatementNode* n, const Type* retType, const LValue* lv)
	{
		if (const LValue* local = OwningLocal(lv))
			return m_Ctx.Fail(n, QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, "cannot return a reference to the local '" +
				m_Ctx.out->GetSlot(local->slot).name + "': it ends when the function returns"));
		if (!static_cast<const ReferenceType*>(retType)->IsConstReferent())
			if (Core::String why = ReadOnlyReason(m_Ctx, lv); !why.empty())
				return m_Ctx.Fail(n, QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, "cannot return a non-const reference to a const object: " + why));
		return m_Ctx.AddressAs(retType, lv);
	}

	// What a reference return gives for `node`: a location's address, or for a conditional the address of the location
	// its condition chooses, each branch checked as a returned location (BUG-398).
	QueryResult<const HIRNode*> StmtLowerer::ReferenceTo(const ReturnStatementNode* n, const Type* retType, const AstNode* node)
	{
		while (const auto* parenthesized = dynamic_cast<const ParenthesizedExpressionNode*>(node)) node = parenthesized->GetExpression().Get();
		if (const auto* choice = dynamic_cast<const TernaryExpressionNode*>(node)) return ChosenReference(n, retType, choice);
		auto v = ExprLowerer(m_Ctx).ResolveFor(node, retType);
		if (!v.Ok()) return QueryResult<const HIRNode*>::Fail(v.error, v.message);
		if (!v.value.isLValue)
			return m_Ctx.Fail(n, QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, "cannot return a reference to a temporary from '" +
				FunctionName() + "': return a location (a variable, a field, an element), not a value"));
		return ReturnReference(n, retType, v.value.lvalue);
	}

	QueryResult<const HIRNode*> StmtLowerer::ChosenReference(const ReturnStatementNode* n, const Type* retType, const TernaryExpressionNode* choice)
	{
		auto cond = ExprLowerer(m_Ctx).ResolveExpr(choice->GetCondition().Get());
		if (!cond.Ok()) return QueryResult<const HIRNode*>::Fail(cond.error, cond.message);
		if (Core::String why = ImplicitConversion::ConditionProblem(m_Ctx, cond.value.type); !why.empty())
			return m_Ctx.Fail(choice->GetCondition().Get(), QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, why));
		auto chosen = ReferenceTo(n, retType, choice->GetThenBranch().Get());
		if (!chosen.Ok()) return chosen;
		auto other = ReferenceTo(n, retType, choice->GetElseBranch().Get());
		if (!other.Ok()) return other;
		return QueryResult<const HIRNode*>(m_Ctx.out->NewNode<TernaryNode>(retType, m_Ctx.LoadIfNeeded(cond.value), chosen.value, other.value));
	}

	// `return F();` in a void function returns F's void result; any other value has nowhere to go.
	QueryResult<const HIRNode*> StmtLowerer::ReturnFromVoid(const ReturnStatementNode* n, const Value& value)
	{
		const Type* got = TypeHandle::UnwrapToValue(value.type);
		if (got && got->GetKind() != TypeKind::Void)
			return m_Ctx.Fail(n, QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, "'" + FunctionName() +
				"' returns void, so 'return' cannot give it a value of type '" + got->GetName() + "'"));
		return m_Ctx.LoadIfNeeded(value);
	}

	Core::String StmtLowerer::FunctionName() const
	{
		if (!m_Ctx.fn) return "?";
		return m_Ctx.fn->name.empty() ? m_Ctx.fn->mangledName : m_Ctx.fn->name;
	}

	QueryResult<const HIRNode*> StmtLowerer::LowerReturnValue(const ReturnStatementNode* n, const Type* retType)
	{
		// A REFERENCE return binds the ADDRESS of the location (of `p` itself in `int32*& Ref() { return p; }`).
		if (retType && retType->GetKind() == TypeKind::Reference) return ReferenceTo(n, retType, n->GetExpression().Get());

		ExprLowerer expr(m_Ctx);
		auto v = expr.ResolveFor(n->GetExpression().Get(), retType);
		if (!v.Ok()) return QueryResult<const HIRNode*>::Fail(v.error, v.message);

		// A by-value object leaves the function as a relocated local - left zero, so its scope-exit
		// destruction does nothing - or as a copy of anything else (docs/language/06).
		const Type* rv = TypeHandle::UnwrapToValue(retType);
		if (!rv || rv->GetKind() == TypeKind::Void) return ReturnFromVoid(n, v.value);
		if ((rv->GetKind() == TypeKind::Object || rv->GetKind() == TypeKind::Array) && TypeHandle::UnwrapToValue(v.value.type) == rv)
		{
			Lifecycle life(m_Ctx);
			if (v.value.isLValue && OwnedByThisFunction(v.value.lvalue) && life.AssignmentNeedsLifecycle(rv))
				return life.Take(v.value.lvalue);
		}
		// Everything else converts as an initialization of the return type would (docs/language/04 4.9).
		ImplicitConversion::WarnIfNarrowing(m_Ctx, n->GetExpression().Get(), v.value, retType);
		auto converted = ConvertedValue(m_Ctx, v.value, retType);
		if (converted.Ok() || converted.error == QueryError::Ambiguous || dynamic_cast<const SignatureType*>(rv)) return m_Ctx.Fail(n, converted);
		const Type* got = TypeHandle::UnwrapToValue(v.value.type);
		return m_Ctx.Fail(n, QueryResult<const HIRNode*>::Fail(converted.error, "return type mismatch in '" +
			FunctionName() + "': got '" + Core::String(got ? got->GetName() : "?") +
			"', function returns '" + rv->GetName() + "': " + converted.message));
	}

	QueryResult<const HIRNode*> StmtLowerer::FullExpression(const AstNode* e)
	{
		const size_t mark = m_Ctx.pendingTemporaries.size();
		ExprLowerer expr(m_Ctx);
		auto v = expr.ResolveExpr(e);
		if (!v.Ok()) return QueryResult<const HIRNode*>::Fail(v.error, v.message);
		return QueryResult<const HIRNode*>(Lifecycle(m_Ctx).EndFullExpression(m_Ctx.LoadIfNeeded(v.value), mark));
	}

	QueryResult<const HIRNode*> StmtLowerer::Condition(const AstNode* e)
	{
		auto value = FullExpression(e);
		if (!value.Ok()) return value;
		if (Core::String why = ImplicitConversion::ConditionProblem(m_Ctx, value.value->type); !why.empty())
			return m_Ctx.Fail(e, QueryResult<const HIRNode*>::Fail(QueryError::NotAValue, why));
		return value;
	}

	QueryResult<void> StmtLowerer::LowerIf(const IfStatementNode* n, HIRNode* region)
	{
		auto cond = Condition(n->GetCondition().Get());
		if (!cond.Ok()) return QueryResult<void>::Fail(cond.error, cond.message);

		IfNode* node = m_Ctx.out->NewNode<IfNode>(cond.value);
		static_cast<BlockNode*>(region)->stmts.push_back(node);

		node->thenBlock = MakeBodyRegion();
		{
			RegionScope thenScope(m_Ctx, node->thenBlock, "if-then");
			if (auto r = LowerBody(n->GetThenBody().Get(), n->GetThenSingleStatement().Get(), node->thenBlock); !r.Ok()) return r;
		}
		if (!n->GetElseBranch()) return QueryResult<void>::Success();
		node->elseBlock = MakeBodyRegion();
		RegionScope elseScope(m_Ctx, node->elseBlock, "if-else");
		return LowerElse(n->GetElseBranch().Get(), node->elseBlock);
	}

	QueryResult<void> StmtLowerer::LowerElse(const AstNode* elseBranch, BlockNode* target)
	{
		// An `else if` is a nested IfNode inside the else region; otherwise a block or a single statement.
		if (const auto* elseIf = dynamic_cast<const IfStatementNode*>(elseBranch)) return LowerIf(elseIf, target);
		const auto* blk = dynamic_cast<const CodeBlockNode*>(elseBranch);
		return LowerBody(blk, blk ? nullptr : elseBranch, target);
	}

	QueryResult<void> StmtLowerer::LowerSwitch(const SwitchStatementNode* n, HIRNode* region)
	{
		auto cond = FullExpression(n->GetCondition().Get());
		if (!cond.Ok()) return QueryResult<void>::Fail(cond.error, cond.message);
		const Type* selector = TypeHandle::UnwrapToValue(cond.value->type);
		if (Core::String why = CaseLabels::SelectorProblem(selector); !why.empty())
			return m_Ctx.Fail(n->GetCondition().Get(), QueryResult<void>::Fail(QueryError::NotAValue, why));

		SwitchNode* node = m_Ctx.out->NewNode<SwitchNode>(cond.value);
		m_Ctx.PushSwitch(node->breakLabel); // a break in a case exits the switch (innermost rule)
		static_cast<BlockNode*>(region)->stmts.push_back(node);

		// Each case owns an entry label: `fallthrough;` and a stacked empty label jump to the next case's entry.
		struct CaseSlot { const SwitchCaseNode* cn; BlockNode* block; uint32 entry; };
		std::vector<CaseSlot> slots;
		CaseLabels labels(m_Ctx, selector);
		for (const auto& c : n->GetCases())
		{
			const SwitchCaseNode* cn = c.Get();
			BlockNode* caseBlock = MakeBodyRegion();
			uint32 entry = m_Ctx.NewLabel();
			if (cn->Kind() == SwitchCaseNode::CaseKind::Default)
			{
				if (node->defaultBlock)
				{
					m_Ctx.PopSwitch();
					return m_Ctx.Fail(cn, QueryResult<void>::Fail(QueryError::NotAValue, "a switch has one 'default': this is its second"));
				}
				node->defaultBlock = caseBlock;
				node->defaultEntry = entry;
			}
			else
			{
				auto v = labels.Add(cn->GetConstantExpr().Get());
				if (!v.Ok()) { m_Ctx.PopSwitch(); return m_Ctx.Fail(cn, QueryResult<void>::Fail(v.error, v.message)); }
				SwitchCase sc; sc.value = v.value; sc.block = caseBlock; sc.entryLabel = entry;
				node->cases.push_back(sc);
			}
			slots.push_back({ cn, caseBlock, entry });
		}

		for (size_t i = 0; i < slots.size(); ++i)
		{
			const uint32* next = i + 1 < slots.size() ? &slots[i + 1].entry : nullptr;
			if (auto r = LowerCase(*slots[i].cn, slots[i].block, next); !r.Ok()) { m_Ctx.PopSwitch(); return r; }
		}

		m_Ctx.PopSwitch();
		return QueryResult<void>::Success();
	}

	// A case runs its statements. `fallthrough;`, its last statement, jumps into the next case; an empty case
	// (a stacked label) continues into the next one; any other case that can run off its end is an error
	// unless it is the last (D-39).
	QueryResult<void> StmtLowerer::LowerCase(const SwitchCaseNode& cn, BlockNode* block, const uint32* next)
	{
		RegionScope caseScope(m_Ctx, block, "switch-case");
		const auto& statements = cn.GetStatements();
		for (uint32 k = 0; k < statements.Count(); ++k)
		{
			const AstNode* statement = statements[k].Get();
			if (statement->Type() != NodeType::FallthroughStatement)
			{
				if (auto r = LowerStmt(statement, block); !r.Ok()) return r;
				continue;
			}
			if (k + 1 < statements.Count())
				return m_Ctx.Fail(statement, QueryResult<void>::Fail(QueryError::NotAValue, "'fallthrough;' must be the last statement of its case"));
			if (!next)
				return m_Ctx.Fail(statement, QueryResult<void>::Fail(QueryError::NotAValue, "'fallthrough;' in the last case has no case to continue into"));
			block->stmts.push_back(m_Ctx.out->NewNode<JumpNode>(*next));
			return QueryResult<void>::Success();
		}
		if (!next) return QueryResult<void>::Success();
		if (statements.Count() == 0)
		{
			block->stmts.push_back(m_Ctx.out->NewNode<JumpNode>(*next));
			return QueryResult<void>::Success();
		}
		if (Reachability::CompletesNormally(*block))
			return m_Ctx.Fail(&cn, QueryResult<void>::Fail(QueryError::NotAValue,
				"this case falls into the next one: end it with 'break;' or 'fallthrough;'"));
		return QueryResult<void>::Success();
	}

	QueryResult<void> StmtLowerer::LowerBreak(const BreakStatementNode* n, HIRNode* region)
	{
		if (!m_Ctx.HasBreakTarget()) return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "'break' with no enclosing loop or switch"));
		if (m_Ctx.BreakLeavesDefer()) return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "'break' may not leave a defer or finally body"));
		static_cast<BlockNode*>(region)->stmts.push_back(m_Ctx.out->NewNode<BreakNode>(m_Ctx.InnerBreak()));
		return QueryResult<void>::Success();
	}

	QueryResult<void> StmtLowerer::LowerContinue(const ContinueStatementNode* n, HIRNode* region)
	{
		if (!m_Ctx.HasContinueTarget()) return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "'continue' with no enclosing loop"));
		if (m_Ctx.ContinueLeavesDefer()) return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "'continue' may not leave a defer or finally body"));
		static_cast<BlockNode*>(region)->stmts.push_back(m_Ctx.out->NewNode<ContinueNode>(m_Ctx.InnerContinue()));
		return QueryResult<void>::Success();
	}

	QueryResult<void> StmtLowerer::LowerGoto(const GotoStatementNode* n, HIRNode* region)
	{
		if (m_Ctx.deferDepth > 0)
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "'goto' is not allowed inside a defer or finally body"));
		const Core::String name = n->GetLabelNameToken().GetText();
		uint32 id = m_Ctx.LabelIdFor(name);
		m_Ctx.jumps.NoteGoto(id, name, n->startLocation);
		static_cast<BlockNode*>(region)->stmts.push_back(m_Ctx.out->NewNode<GotoNode>(id));
		return QueryResult<void>::Success();
	}

	QueryResult<void> StmtLowerer::LowerLabel(const LabelStatementNode* n, HIRNode* region)
	{
		if (m_Ctx.deferDepth > 0)
			return m_Ctx.Fail(n, QueryResult<void>::Fail(QueryError::NotAValue, "labels are not allowed inside a defer or finally body"));
		const Core::String name = n->GetLabelNameToken().GetText();
		uint32 id = m_Ctx.LabelIdFor(name);
		if (auto r = m_Ctx.jumps.NoteLabel(id, name, n->startLocation); !r.Ok()) return m_Ctx.Fail(n, r);
		static_cast<BlockNode*>(region)->stmts.push_back(m_Ctx.out->NewNode<LabelNode>(id));
		// The parser binds the statement after ':' to the label (`skip: v = 2;`); it runs right after the marker.
		if (n->GetStatement())
			return LowerStmt(n->GetStatement().Get(), region);
		return QueryResult<void>::Success();
	}
} // namespace Scripting
