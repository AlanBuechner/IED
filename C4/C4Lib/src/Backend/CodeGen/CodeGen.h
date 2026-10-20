#pragma once
// =====================================================================
// CodeGen - HIR-driven LLVM emitter (see docs/compiler/04-codegen-and-abi.md).
// Public umbrella for the new backend. Every subsystem is a self-contained
// .h/.cpp pair (one-to-one):
//
//   Abi.{h,cpp}             THE calling-convention brick: Windows x64 aggregate rules (04)
//   ModuleContext.{h,cpp}   program-wide LLVM state: module, type map, struct fill, function sigs
//   IRContext.{h,cpp}       per-function emission state: builder, slot allocas, label registry, this
//   ExprEmitter.{h,cpp}     rvalue nodes -> SSA values + lvalue address resolution + the GC store
//   CallEmitter.{h,cpp}     calls: hidden params, arguments per the Abi, direct/virtual/indirect callee
//   ObjectEmitter.{h,cpp}   new / T(...) / init lists / delete
//   CleanupStack.{h,cpp}    scope-exit cleanups, try and handler entries (04 scope exit)
//   RegionEmitter.{h,cpp}   the block-owning walker: control flow + straight-line statements (03)
//   VtableEmitter.{h,cpp}   vtables per vptr location, this-adjusting thunks, vptr stores
//   RttiEmitter.{h,cpp}     MSVC RTTI: type descriptors, hierarchy descriptors, object locators
//   EhTables.{h,cpp}        MSVC throw info and catch handler operands
//   MsvcNames.{h,cpp}       MSVC decorated type names (the RTTI / EH type identity)
//   PeTables.{h,cpp}        image-relative table helpers shared by RTTI and EH
//   FunctionEmitter.{h,cpp} one HIRFunction -> its llvm::Function (+ nested functions)
//   CodeGenDriver.{h,cpp}   lower a ProgramModel to ONE module: types, decls, globals, bodies, verify
//
// Entry point:
//   bool CodeGenDriver::Lower(ProgramModel&, const Core::String& entryName);
// Build the whole module from the model (the model is non-const: lowering registers function-local
// scopes and template bindings into it). Returns true when the module verifies.
// =====================================================================
#include "Backend/CodeGen/Abi.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Backend/CodeGen/IRContext.h"
#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/RegionEmitter.h"
#include "Backend/CodeGen/FunctionEmitter.h"
#include "Backend/CodeGen/CodeGenDriver.h"
