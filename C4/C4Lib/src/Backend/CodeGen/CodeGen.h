#pragma once
// =====================================================================
// CodeGen - the LLVM emitter of MIR and HIR expressions (see docs/compiler/04-codegen-and-abi.md, 06-mir.md).
// Public umbrella of the emitter. It emits what Backend/Lowering lowered and decides nothing about
// what is lowered. Every subsystem is a self-contained .h/.cpp pair (one-to-one):
//
//   Abi.{h,cpp}               THE calling-convention brick (04)
//   ModuleContext.{h,cpp}     one module's LLVM state: module, type map, struct fill, function sigs, globals
//   IRContext.{h,cpp}         per-function emission state: builder, slot allocas, this, the block being emitted
//   ExprEmitter.{h,cpp}       rvalue nodes -> SSA values + lvalue address resolution + the GC store
//   CallEmitter.{h,cpp}       calls: hidden params, arguments per the Abi, direct/virtual/indirect callee
//   ObjectEmitter.{h,cpp}     new / T(...) / init lists / delete
//   MirEmitter.{h,cpp}        one function's MIR: its blocks, statements and terminators (06)
//   EhLowering.h              the target's exception scheme: FuncletLowering (Windows) or LandingPadLowering (Linux)
//   VtableEmitter.{h,cpp}     vtables per vptr location, this-adjusting thunks, vptr stores
//   RttiEmitter.{h,cpp}       MSVC RTTI: type descriptors, hierarchy descriptors, object locators
//   EhTables.{h,cpp}          MSVC throw info and catch handler operands
//   FunctionEmitter.{h,cpp}   one HIRFunction -> its llvm::Function (+ nested functions)
//   PartitionSet.{h,cpp}      the program's modules (D-81): placement, emission on threads, census counters
//   StartupEmitter.{h,cpp}    global and thread starts, the entry point's @main, the GC stats printer
//   ReflectionEmitter.{h,cpp} thunks made locals, the reflection image and its registration (D-82)
//   ImportEmitter.{h,cpp}     each partition's available_externally copies of small functions
//   ProgramEmitter.{h,cpp}    a LoweredProgram -> verified modules, in the assembly order of 04
//
// Entry point: ProgramEmitter::Emit(model, lowered program, settings, pool, error), after
// ProgramLowering::Run (Backend/ProgramBackend runs both).
// =====================================================================
#include "Backend/CodeGen/Abi.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/CodeGen/IRContext.h"
#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/MirEmitter.h"
#include "Backend/CodeGen/FunctionEmitter.h"
#include "Backend/CodeGen/ProgramEmitter.h"
