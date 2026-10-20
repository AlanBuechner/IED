#pragma once
#include "Backend/HIR/HIRNodes.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <vector>

namespace Scripting
{
	/// One scope-exit obligation or exception scope between the function entry and the current emission
	/// point (docs/compiler/04-codegen-and-abi.md, "Scope exit and unwinding").
	struct CleanupEntry
	{
		// Cleanup runs HIR statements; Abandon is a runtime call only an unwinding exception makes (a once
		// region giving up its guard).
		enum class Kind { Cleanup, Abandon, Try, Handler };

		Kind kind = Kind::Cleanup;
		const BlockNode* body = nullptr;          // Cleanup: the statements to run when the scope is left
		bool exceptionalOnly = false;             // Cleanup: only an unwinding exception runs it
		llvm::Function* abandon = nullptr;        // Abandon: the function called, with `abandonArgument`
		llvm::Value* abandonArgument = nullptr;
		llvm::BasicBlock* dispatch = nullptr;     // Try: the block holding the try's catchswitch
		llvm::CatchPadInst* handlerPad = nullptr; // Handler: the catchpad of the clause being emitted
		llvm::Value* parentPad = nullptr;         // the funclet this entry was pushed in (null = none)
		size_t floor = 0;                         // the unwind floor in effect when it was pushed
		llvm::BasicBlock* ehPad = nullptr;        // Cleanup, Abandon: its cleanuppad block, created on first need

		bool HasPad() const { return kind == Kind::Cleanup || kind == Kind::Abandon; }
	};

	/// The stack of CleanupEntry in nesting order. Only the top is ever pushed or popped, so an entry below
	/// the top always sits at the index it was pushed at - lazily created pads can be cached on the entry.
	class CleanupStack
	{
	public:
		size_t Depth() const { return m_Entries.size(); }
		CleanupEntry& At(size_t i) { return m_Entries[i]; }
		const CleanupEntry& At(size_t i) const { return m_Entries[i]; }

		void Push(const CleanupEntry& e) { m_Entries.push_back(e); }
		CleanupEntry Pop();
		void PopTo(size_t depth);

		// Remove the entries at and above `depth` (while a cleanup below them is emitted) and put them back.
		std::vector<CleanupEntry> DetachFrom(size_t depth);
		void Restore(std::vector<CleanupEntry>&& entries);

		// The innermost Handler entry at or above `floor`, or null.
		llvm::CatchPadInst* InnermostHandler(size_t floor) const;

	private:
		std::vector<CleanupEntry> m_Entries;
	};
} // namespace Scripting
