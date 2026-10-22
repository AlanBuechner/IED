#pragma once
#include "Backend/CodeGen/IRContext.h"
#include "Backend/CodeGen/ExprEmitter.h"
#include <unordered_map>
#include <vector>

namespace Scripting
{
	/// The block-owning walker (doc 03): emits statements, creates the control-flow blocks, and owns scope
	/// exit. Every BlockNode is a frame; every `DeferNode` pushes a cleanup on the IRContext's CleanupStack.
	/// Every exit edge - fall-through, break, continue, fallthrough, goto, return - runs the cleanups it
	/// crosses inline (leaving crossed catch handlers with catchret), and the first exception edge that needs
	/// a cleanup creates its cleanuppad (CleanupPadFor), which runs the same body and unwinds outward.
	class RegionEmitter : public RegionServices
	{
	public:
		explicit RegionEmitter(IRContext& ctx);
		~RegionEmitter() override;

		void EmitBlock(const BlockNode* block);
		/// A noexcept function's body: an exception that would leave it stops the program.
		void EmitNoexceptBody(const BlockNode* root);
		llvm::BasicBlock* CleanupPadFor(size_t index) override;
		void EmitNestedBlock(const BlockNode* block) override { EmitBlock(block); }

	private:
		struct Frame { const BlockNode* block = nullptr; size_t statementDepth = 0; };
		struct Label { llvm::BasicBlock* block = nullptr; size_t depth = 0; bool placed = false; const BlockNode* owner = nullptr; };

		void EmitStatement(const HIRNode* stmt);
		void EmitLocalDecl(const LocalDeclNode* d);
		void EmitOnce(const OnceNode* n);
		void EmitThreadOnce(const OnceNode* n);
		void EmitAtExit(const AtExitNode* n);
		void EmitReturn(const ReturnNode* r);
		void EmitReturnValue(llvm::Value* v, const Type* ret);
		/// The call `return f(...)` returns when it can build its result in this function's own result pointer.
		const CallNode* ResultCall(const ReturnNode* r);
		void EmitGoto(const GotoNode* g);
		void EmitLabel(const LabelNode* l);
		void EmitIf(const IfNode* n);
		void EmitWhile(const WhileNode* n);
		void EmitFor(const ForNode* n);
		void EmitForLoop(const ForNode* n);
		void EmitSwitch(const SwitchNode* n);
		void EmitTryCatch(const TryNode* n);
		void EmitHandler(const TryNode::CatchClause& clause, llvm::CatchSwitchInst* dispatch, llvm::BasicBlock* handlerBlock, llvm::BasicBlock* join);
		void EmitThrow(const ThrowNode* n);

		// Scope exit. ExitCleanups emits the exit of every entry above `depth` at the insert point (the stack is
		// unchanged afterwards); EndScope does the same for a scope that is closing and pops its entries.
		void ExitCleanups(size_t depth);
		void EndScope(size_t depth);
		void ExitEntry(const CleanupEntry& e);
		void BranchThroughCleanups(size_t depth, llvm::BasicBlock* target);
		void PushCleanup(const BlockNode* body, bool exceptionalOnly);
		void PushAbandon(llvm::Function* abandon, llvm::Value* argument);
		void EmitCleanupBody(const CleanupEntry& e);
		size_t ForwardJumpDepth(const BlockNode* owner) const;

		void DeclareLabels(const BlockNode* block);
		void DefineLabel(uint32 id, llvm::BasicBlock* block);
		llvm::BasicBlock* NewBlock(const char* name);
		bool IsOpen() const;
		void ContinueAfterTerminator();

		IRContext& m_Ctx;
		ExprEmitter m_Expr;
		std::vector<Frame> m_Frames;
		std::unordered_map<uint32, Label> m_Labels;
	};
} // namespace Scripting
