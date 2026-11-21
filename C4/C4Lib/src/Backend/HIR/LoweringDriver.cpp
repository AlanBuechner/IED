#include "Backend/HIR/LoweringDriver.h"
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/StmtLowerer.h"
#include "Backend/HIR/MethodBody.h"
#include "Backend/HIR/LambdaLowerer.h"
#include "Backend/HIR/TemplateBindings.h"
#include "Backend/MIR/CompileTimeCheck.h"
#include "Backend/MIR/MirMoves.h"
#include "Backend/MIR/MirReach.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Model/Types/ReferenceType.h" // T& parameter - passed by address, registered as Pointer(pointee)
#include "Model/TypeHandle.h"           // UnwrapToValue (alias/reference -> concrete type)
#include "Model/Abstractness.h"

namespace Scripting
{
	namespace
	{
		// `args` of `Ts... args` names the parameters that are its elements (D-19), kept by the lowering's cursor.
		void BindParameterPacks(ScopeCursor& cursor, LoweringContext& ctx, const FunctionRecord& record)
		{
			for (const FunctionRecord::ParameterPack& pack : record.parameterPacks)
			{
				std::vector<SymbolInfo> elements;
				for (uint32 i = pack.first; i < pack.first + pack.count; ++i)
					if (const SymbolInfo* param = ctx.queries().FindLocal(record.paramNames[i])) elements.push_back(*param);
				SymbolInfo sym; sym.kind = SymbolKind::Variable; sym.definingUnit = 0; sym.packElements = cursor.KeepPack(std::move(elements));
				ctx.queries().RegisterLocal(pack.name, sym);
			}
		}
	}

	namespace
	{
		// D-55: control may not run off the end of a function that returns a value; `main` returns 0 there, as in C.
		QueryResult<void> ReturnsOnEveryPath(const FunctionRecord& record, bool isLambda, const CodeBlockNode* body, const MirBody& mir)
		{
			const Type* returned = record.type ? TypeHandle::UnwrapToValue(record.type->GetReturnType()) : nullptr;
			if (!body || !returned || returned->GetKind() == TypeKind::Void || IsEntryPoint(record, "main") ||
				!MirReach::FallsOffEnd(mir))
				return QueryResult<void>::Success();
			const Core::String function = isLambda ? Core::String("the lambda")
				: "non-void function '" + (record.name.empty() ? record.mangledName : record.name) + "'";
			auto missing = QueryResult<void>::Fail(QueryError::NotAValue, "control reaches the end of " + function +
				": return a '" + returned->GetName() + "' on every path");
			missing.loc = body->GetCloseBraceToken().GetStartLocation();
			return missing;
		}

		// What the body's lowering recorded for its last-use moves (D-05).
		MirMoveSites MoveSitesOf(const LoweringContext& ctx)
		{
			MirMoveSites sites;
			for (const LoweringContext::TransferSite& transfer : ctx.transfers) sites.transfers.push_back({ transfer.node, transfer.slot });
			for (const auto& [slot, cleanup] : ctx.localCleanups) sites.cleanups[slot] = { cleanup.cleanup, cleanup.beginsWith };
			return sites;
		}

		bool HasErrorSince(const ErrorTracker& errors, size_t first)
		{
			const Core::Vector<CompilationError>& reported = errors.GetErrors();
			for (size_t i = first; i < reported.Count(); ++i)
				if (reported[static_cast<uint32>(i)].severity != ErrorSeverity::Warning) return true;
			return false;
		}
	}

	HIRFunction LowerFunction(ProgramModel& model, ErrorTracker& errors, const FunctionRecord* record)
	{
		HIRFunction fn;
		if (!record || !HasBody(*record)) return fn; // declaration only: nothing to lower

		// A compiler-written lifecycle function has no source body (MethodBody writes it).
		const CodeBlockNode* body = dynamic_cast<const CodeBlockNode*>(record->bodyNode);
		if (!body && record->synthesized == LifecycleOp::None) return fn;

		fn.record = record;
		if (body) fn.location = body->startLocation; // the parameters and the prologue belong to the opening brace
		if (Core::String why = Abstractness::Signature(model, *record); !why.empty())
		{
			errors.AddError(body ? body->startLocation : SourceLocation(), why);
			return fn;
		}
		LoweringContext ctx(&model, &errors);
		ctx.fn = record;
		ctx.out = &fn;
		ctx.unitId = record->LookupUnit(); // a template-made body sees what its template's file includes
		ctx.enclosingType = record->containingType;
		ctx.primitiveThis = record->primitiveReceiverType; // a primitive receiver (extension method) is a pointer this
		ctx.scope = record->scope; // the function's enclosing namespace/class - unqualified type lookup context

		// This lowering's own place in the scope tree, so lowerings on other threads never see its locals; then the
		// function's layer: parameters + locals live here for the whole body.
		ScopeCursor cursor(*model.Symbols(), ctx.unitId);
		ScopeGuard fnScope = ctx.queries().PushScope(Scope::Kind::Function, record->mangledName);

		// Template bindings: a body made from a template sees its parameters (T := int32, N := 4) as names in
		// the function's own layer. The record's own bindings come first (a function template, an extension
		// template, an out-of-line body naming the parameters differently), then its type's.
		BindTemplateParameters(ctx, model.FunctionInstantiationBindings(record));
		if (record->containingType) BindTemplateParameters(ctx, model.TypeInstantiationBindings(record->containingType));

		// Parameters become slots (class Param) bound as locals. Names come from the RECORD (the interned
		// FunctionType drops them). A reference parameter is passed BY ADDRESS: its symbol type is a pointer
		// to the referent and reads go through it. `this` is NOT a slot: it is the implicit receiver.
		for (uint32 i = 0; i < record->paramNames.Count(); ++i)
		{
			const Type* ptype = record->type ? record->type->GetParameters()[i].m_Type : nullptr;
			uint32 slot = ctx.AllocSlot(ptype, record->paramNames[i], SlotClass::Param);
			SymbolInfo sym; sym.kind = SymbolKind::Variable; sym.type = ptype; sym.definingUnit = 0; sym.isConst = record->ParamIsConst(i);
			bool isReference = ptype && ptype->GetKind() == TypeKind::Reference;
			bool refersToConst = false;
			if (isReference)
			{
				const auto* ref = static_cast<const ReferenceType*>(ptype);
				sym.type = ctx.queries().TypeStore()->Pointer(TypeHandle::UnwrapToValue(ref->GetReferentType()));
				refersToConst = ref->IsConstReferent();
			}
			ctx.BindLocal(record->paramNames[i], sym, slot, isReference, refersToConst);
		}
		BindParameterPacks(cursor, ctx, *record);

		// A lambda's call method: its captures are names bound to the closure's fields (docs/language/07).
		if (const Closure* closure = model.ClosureOf(record->containingType)) EnterClosureBody(ctx, *closure);

		// The root region: a block that is also the scope carrier for the body.
		fn.root = fn.NewNode<BlockNode>(nullptr);
		size_t errorsBefore = errors.GetErrors().Count();
		auto r = LowerFunctionBody(ctx, record, body, fn.root);
		if (r.Ok()) r = ctx.jumps.Validate();
		// The complete tree's control-flow graph: the flow checks and the last-use moves run on it, and codegen emits it.
		const std::optional<MirMoveSites> moves = r.Ok() && !ctx.transfers.empty() ? std::optional(MoveSitesOf(ctx)) : std::nullopt;
		if (r.Ok())
		{
			const std::optional<MirBuildOptions> options = moves ? std::optional(moves->BuildOptions()) : std::nullopt;
			fn.mir.reset(new MirBody(MirBuilder(fn, options ? &*options : nullptr).Build()));
			r = MirReach::CheckCases(*fn.mir);
		}
		if (r.Ok()) r = ReturnsOnEveryPath(*record, model.ClosureOf(record->containingType) != nullptr, body, *fn.mir);
		if (r.Ok()) r = CheckCompileTimeBody(fn);
		if (!r.Ok())
		{
			// Contract: an incomplete lowering ALWAYS reports why. Some paths return a raw Fail without
			// recording it; guarantee this function contributed at least one error (a warning does not say why).
			if (!HasErrorSince(errors, errorsBefore))
				errors.AddError(r.loc.line ? r.loc : (body ? body->startLocation : SourceLocation()), r.message);
			fn.mir.reset();
			return fn; // incomplete tree, codegen skips it
		}

		if (moves) MirMoves::Apply(fn, *fn.mir, *moves, ctx.queries().TypeStore()->Builtin(TypeKind::Bool));
		fn.complete = true;
		return fn;
	}
} // namespace Scripting
