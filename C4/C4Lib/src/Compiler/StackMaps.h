#pragma once
#include <string>

namespace llvm { class Module; class TargetMachine; }

namespace Scripting
{
	/// Stack maps (R16 phase 3, `-fstack-maps`, docs/compiler/04 "GC emission"): the collector reads a C4 frame's managed
	/// pointers from the slots LLVM's stack map names for the call the frame waits in, instead of the whole frame.

	/// The address space of a managed pointer (ModuleContext::ManagedPtr): non-integral, so the optimizer never turns one
	/// into an integer, which no stack map would name.
	constexpr unsigned c_ManagedAddressSpace = 1;
	/// The GC strategy a function with statepoints names (`gc "c4"`): every pointer of the managed space is a GC pointer.
	constexpr const char* c_StackMapsGc = "c4";

	/// Marks `module` for stack maps: a module flag (the passes below and the object writer read it) and its data layout
	/// with the managed address space non-integral.
	void MarkForStackMaps(llvm::Module& module);
	bool UsesStackMaps(const llvm::Module& module);
	/// `layout` with the managed address space non-integral (a target machine's layout has no such space).
	std::string StackMapsLayout(const std::string& layout);

	/// Before the optimizer: a C library function a marked module declares with managed pointers is no builtin. LLVM
	/// rewrites a builtin's call into calls of library functions it declares with raw pointers (`memcmp(..) == 0` into
	/// `bcmp` where the C library has one) and passes those the managed pointers, which is no valid IR.
	void KeepLibraryCallsAsWritten(llvm::Module& module);

	/// After the optimizer, every call of a marked module's functions becomes a statepoint that names the managed pointers
	/// live across it and the frame's stack slots that may hold one, so code generation writes their places into the
	/// module's stack map (`.llvm_stackmaps`). A call left a leaf has no record, and a frame waiting in one is read whole:
	/// an invoke and a call in a funclet (RewriteStatepointsForGC cannot take them), inline assembly, a variadic call (a
	/// statepoint loses what the convention does for one), one that never returns (a throw), one in tail position (it
	/// keeps its sibling-call optimization), one that passes an aggregate on the stack (that copy is in the caller's
	/// outgoing area, which no record names). Does nothing to a module not marked. `machine` (may be null) gives the
	/// rewrite the target's costs.
	void InsertStatepoints(llvm::Module& module, llvm::TargetMachine* machine);

	/// An ELF object's `.llvm_stackmaps`, which holds absolute addresses of functions, made writable (a position
	/// independent program relocates it as it loads), kept by the linker's garbage collection, and named
	/// `llvm_stackmaps`, which the linker brackets with the __start_/__stop_ symbols the runtime finds it by. False when
	/// `object` is no 64-bit little-endian ELF object; true, unchanged, when it has no stack map.
	bool PrepareElfStackMaps(std::string& object);
}
