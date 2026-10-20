#pragma once
// =====================================================================
// HIR (High Intermediate Representation) - program-model-driven lowering.
// Public umbrella for the new lowering system (see docs/compiler/03-hir.md).
// Every subsystem is a self-contained .h/.cpp pair (one-to-one):
//
//   HIRValue.h          Value / LValue: the rvalue-lvalue two-ness (03.2)
//   HIRNodes.h          the closed node taxonomy + HIRFunction arena (03.5, 03.7)
//   ModelQueries.{h,cpp}    the ONE model-facing surface (queries + scope + op maps)
//   LoweringContext.h   per-function state composing queries + slots + diagnostics (03.4)
//   ExprLowerer.{h,cpp}     expression walk: ResolveExpr -> Value (05, expression rows)
//   StmtLowerer.{h,cpp}     statement walk: LowerStmt -> region (05, statement rows)
//   LoweringDriver.{h,cpp}  entry point: set up context, bind params + template aliases, run the walks
//
// Entry point:
//   HIRFunction LowerFunction(ProgramModel&, ErrorTracker&, const FunctionRecord*);
// Lower one function body to a typed tree, or report diagnostics and return an
// incomplete tree (complete == false). Codegen walks only complete trees.
// =====================================================================
#include "Backend/HIR/HIRValue.h"
#include "Backend/HIR/HIRNodes.h"
#include "Backend/HIR/ModelQueries.h"
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/StmtLowerer.h"
#include "Backend/HIR/LoweringDriver.h"
