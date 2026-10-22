#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Parser/Nodes/Statements/SimpleStatementNodes.h" // goto / label statement nodes
#include <functional>

namespace Scripting
{
	struct CodeBlockNode; struct ExpressionStatementNode;
	struct ReturnStatementNode; struct IfStatementNode;
	struct SwitchStatementNode; struct SwitchCaseNode; struct BreakStatementNode; struct ContinueStatementNode;
	struct FunctionDefinitionNode; struct AliasDefinitionNode;

	/// One lexical block while it lowers: its model scope layer and its place on the goto block path.
	/// Every BlockNode a body lowers into is entered through exactly one RegionScope, so the scope chain,
	/// the jump rules and codegen's cleanup frames all see the same nesting.
	class RegionScope
	{
	public:
		RegionScope(LoweringContext& ctx, const BlockNode* block, const char* name);
		~RegionScope();
		RegionScope(const RegionScope&) = delete;
		RegionScope& operator=(const RegionScope&) = delete;

	private:
		LoweringContext& m_Ctx;
		ScopeGuard m_Scope;
	};

	// =====================================================================
	// STATEMENT LOWERER - the statement dispatcher and the plain statements. Declarations, loops and
	// exception/defer statements live in DeclLowerer, LoopLowerer and ExceptionLowerer, which call back
	// into LowerStmt for nested statements. Every block is its own region and scope (RegionScope).
	// =====================================================================
	class StmtLowerer
	{
	public:
		explicit StmtLowerer(LoweringContext& ctx) : m_Ctx(ctx) {}

		QueryResult<void> LowerStmt(const AstNode* node, HIRNode* region);
		/// A body CodeBlock lowered directly into `region` (the function root, a try body, a defer body).
		QueryResult<void> LowerBlock(const CodeBlockNode* n, BlockNode* region, const char* scopeName = "block");
		/// A function body into its root: `between` runs after the first `leading` statements and before the
		/// rest (the lifecycle parts of a create, MethodBody).
		QueryResult<void> LowerFunctionBody(const CodeBlockNode* n, BlockNode* root, uint32 leading,
			const std::function<QueryResult<void>()>& between);
		/// The statements of a control-flow body that is either a braced block or a single statement,
		/// lowered into `target` (the caller has already entered target's RegionScope).
		QueryResult<void> LowerBody(const CodeBlockNode* block, const AstNode* single, BlockNode* target);
		/// A control-flow body: a fresh child BlockNode owned by the control node (never a sibling statement).
		BlockNode* MakeBodyRegion();
		/// A condition or loop step: its value, with its temporaries destroyed as soon as it is computed.
		QueryResult<const HIRNode*> FullExpression(const AstNode* e);
		/// The condition of an `if` or a loop: a full expression with a truth value.
		QueryResult<const HIRNode*> Condition(const AstNode* e);

	private:
		QueryResult<void> Dispatch(const AstNode* node, BlockNode* block);
		bool OwnedByThisFunction(const LValue* lv) const;
		const LValue* OwningLocal(const LValue* lv) const;
		QueryResult<const HIRNode*> ReturnReference(const ReturnStatementNode* n, const Type* retType, const LValue* lv);
		QueryResult<const HIRNode*> ReturnFromVoid(const ReturnStatementNode* n, const Value& value);
		Core::String FunctionName() const;
		QueryResult<void> LowerStatements(const CodeBlockNode* n, BlockNode* target);
		/// Statements [from, to) of `n`, each lowered even after one before it failed, so every failure is reported
		/// where it happened; the result is the first failure.
		QueryResult<void> LowerEach(const CodeBlockNode* n, uint32 from, uint32 to, BlockNode* target);
		QueryResult<void> LowerNestedBlock(const CodeBlockNode* n, HIRNode* region);
		QueryResult<void> LowerLocalType(const AstNode* decl);
		QueryResult<void> LowerLocalAlias(const AliasDefinitionNode* alias);
		QueryResult<void> LowerLocalFunction(const FunctionDefinitionNode* fn);
		QueryResult<void> LowerExprStmt(const ExpressionStatementNode* n, HIRNode* region);
		QueryResult<void> LowerReturn(const ReturnStatementNode* n, HIRNode* region);
		QueryResult<const HIRNode*> LowerReturnValue(const ReturnStatementNode* n, const Type* retType);
		QueryResult<void> LowerIf(const IfStatementNode* n, HIRNode* region);
		QueryResult<void> LowerElse(const AstNode* elseBranch, BlockNode* target);
		QueryResult<void> LowerSwitch(const SwitchStatementNode* n, HIRNode* region);
		QueryResult<void> LowerCase(const SwitchCaseNode& cn, BlockNode* block, const uint32* next);
		QueryResult<void> LowerBreak(const BreakStatementNode* n, HIRNode* region);
		QueryResult<void> LowerContinue(const ContinueStatementNode* n, HIRNode* region);
		QueryResult<void> LowerGoto(const GotoStatementNode* n, HIRNode* region);
		QueryResult<void> LowerLabel(const LabelStatementNode* n, HIRNode* region);

		LoweringContext& m_Ctx;
	};
} // namespace Scripting
