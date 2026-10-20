#pragma once
#include "Backend/CodeGen/ModuleContext.h"
#include <llvm/IR/IRBuilder.h>

namespace Scripting
{
	struct FunctionRecord;

	/// The runtime side of objects with static storage (C4Runtime/StaticStorageRuntimeC.c): the guard a once
	/// region runs behind, and the exit-time destruction of globals, static members and static locals.
	namespace StaticStorage
	{
		/// The guard states the runtime shares (StaticStorageRuntimeC.c, ThreadStorageRuntimeC.c).
		constexpr uint32_t kOnceDone = 1;
		constexpr uint32_t kOnceRunning = 2;

		/// A zeroed `{ int32 state, int32 owner }` guard named `static.once.<name>`.
		llvm::GlobalVariable* NewOnceGuard(ModuleContext& m, const Core::String& name);
		/// A zeroed thread_local `int32 state` guard named `thread.once.<name>`: a thread_local's start on one thread.
		llvm::GlobalVariable* NewThreadOnceGuard(ModuleContext& m, const Core::String& name);
		/// True (i1) when the guarded region has completed: an acquire load of the state.
		llvm::Value* IsOnceDone(llvm::IRBuilder<>& B, llvm::GlobalVariable* guard);

		/// int32 c4_once_begin(guard): non-zero when the caller runs the region (other callers wait).
		llvm::Function* OnceBegin(ModuleContext& m);
		/// void c4_once_end(guard): the region completed.
		llvm::Function* OnceEnd(ModuleContext& m);
		/// void c4_once_abandon(guard): an exception left the region; the next pass runs it again.
		llvm::Function* OnceAbandon(ModuleContext& m);
		/// void c4_at_exit(destroyer, object): `destroyer(object)` runs at exit, the last registered first.
		llvm::Function* AtExit(ModuleContext& m);
		/// void c4_thread_at_exit(destroyer, object): the same when the calling thread ends.
		llvm::Function* ThreadAtExit(ModuleContext& m);
		/// void c4_thread_once_abandon(guard): an exception left a thread's start; its next pass runs it again.
		llvm::Function* ThreadOnceAbandon(ModuleContext& m);
		/// noreturn void c4_thread_once_reentered(): a thread's start reached its own declaration again.
		llvm::Function* ThreadOnceReentered(ModuleContext& m);

		/// `void(ptr object)` running `destroy` on each of `count` elements `stride` bytes apart, last first.
		llvm::Function* ExitDestroyer(ModuleContext& m, const FunctionRecord& destroy, uint64 count, uint64 stride);
	}
} // namespace Scripting
