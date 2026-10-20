#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	class StmtLowerer;
	class ReferenceType;
	struct WhileLoopNode; struct DoWhileLoopNode; struct ForLoopNode;

	/// Loops: while, do-while, for, and range-for (arrays, pointers, __init_list<T>, and the iterator
	/// protocol). A for statement's init clause is a block that encloses the condition, the body and the
	/// step, so locals declared there live (and are destroyed) with the loop.
	class LoopLowerer
	{
	public:
		LoopLowerer(LoweringContext& ctx, StmtLowerer& stmts) : m_Ctx(ctx), m_Stmts(stmts) {}

		QueryResult<void> LowerWhile(const WhileLoopNode* n, BlockNode* region);
		QueryResult<void> LowerDoWhile(const DoWhileLoopNode* n, BlockNode* region);
		QueryResult<void> LowerFor(const ForLoopNode* n, BlockNode* region);

	private:
		QueryResult<void> LowerForInit(const ForLoopNode* n, BlockNode* initBlock);
		QueryResult<void> LowerSteps(const ForLoopNode* n, ForNode* node);
		QueryResult<void> LowerRangeFor(const ForLoopNode* n, BlockNode* region);
		QueryResult<void> LowerRangeForIterator(const ForLoopNode* n, BlockNode* region);
		QueryResult<void> LowerLoopBody(const ForLoopNode* n, BlockNode* body);
		/// The range-for variable of one iteration: a copy of an lvalue element (a fresh one is relocated),
		/// destroyed when the iteration ends.
		QueryResult<void> DeclareLoopVariable(const ForLoopNode* n, const Type* t, const Value& element, BlockNode* body);
		QueryResult<void> DeclareLoopReference(const ForLoopNode* n, const ReferenceType* ref, const Value& element, BlockNode* body);
		uint32 DeclareHidden(BlockNode* initBlock, const Type* t, const Core::String& name, const HIRNode* init, bool isConst = false);

		LoweringContext& m_Ctx;
		StmtLowerer& m_Stmts;
	};
} // namespace Scripting
