#pragma once

namespace llvm { class MachineFunctionPass; }

namespace C4Compiler
{
	/// A machine pass, once the code is laid out (after block placement and funclet layout: LLVM's StackMapLiveness), that
	/// puts each record's label at its call's return address, which the collector looks a frame's record up by. A call
	/// record (Compiler/StackMapFormat: a stackmap of c_CallRecordId, which Compiler/StackMapRecords puts right after its
	/// call at -O0) moves to right after its call's instruction: the register allocator puts the call's result copies and
	/// its reloads between them; one that follows no call of its block goes, and its frame is read whole. On Windows code
	/// generation puts a nop after a call before an epilogue or an exception state's change, and before the label of the
	/// call's record or statepoint: the pass puts that nop after the label itself. Every code generation pipeline has it
	/// (Compiler/StackMaps, PrepareCodeGeneration); it changes nothing in a function without `gc "c4"`.
	llvm::MachineFunctionPass* CreateStackMapPlacement();
}
