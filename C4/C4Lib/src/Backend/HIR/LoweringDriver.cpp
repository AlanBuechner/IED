#include "Backend/HIR/LoweringDriver.h"
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/StmtLowerer.h"
#include "Backend/HIR/MethodBody.h"
#include "Backend/HIR/LambdaLowerer.h"
#include "Backend/HIR/TemplateBindings.h"
#include "Backend/HIR/LastUseMoves.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Model/Types/ReferenceType.h" // T& parameter - passed by address, registered as Pointer(pointee)
#include "Model/TypeHandle.h"           // UnwrapToValue (alias/reference -> concrete type)
#include "Model/Abstractness.h"
#include "Backend/HIR/Reachability.h"

namespace Scripting
{
	namespace
	{
		// `args` of `Ts... args` names the parameters that are its elements (D-19).
		void BindParameterPacks(ProgramModel& model, LoweringContext& ctx, const FunctionRecord& record)
		{
			for (const FunctionRecord::ParameterPack& pack : record.parameterPacks)
			{
				std::vector<SymbolInfo> elements;
				for (uint32 i = pack.first; i < pack.first + pack.count; ++i)
					if (const SymbolInfo* param = ctx.queries().FindLocal(record.paramNames[i])) elements.push_back(*param);
				SymbolInfo sym; sym.kind = SymbolKind::Variable; sym.definingUnit = 0; sym.packElements = model.StorePack(std::move(elements));
				ctx.queries().RegisterLocal(pack.name, sym);
			}
		}
	}

	namespace
	{
		// D-55: control may not run off the end of a function that returns a value; `main` returns 0 there, as in C.
		QueryResult<void> ReturnsOnEveryPath(const FunctionRecord& record, bool isLambda, const CodeBlockNode* body, const BlockNode& root)
		{
			const Type* returned = record.type ? TypeHandle::UnwrapToValue(record.type->GetReturnType()) : nullptr;
			if (!body || !returned || returned->GetKind() == TypeKind::Void || IsEntryPoint(record, "main") ||
				!Reachability::CompletesNormally(root))
				return QueryResult<void>::Success();
			const Core::String function = isLambda ? Core::String("the lambda")
				: "non-void function '" + (record.name.empty() ? record.mangledName : record.name) + "'";
			auto missing = QueryResult<void>::Fail(QueryError::NotAValue, "control reaches the end of " + function +
				": return a '" + returned->GetName() + "' on every path");
			missing.loc = body->GetCloseBraceToken().GetStartLocation();
			return missing;
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
		ctx.unitId = record->definingUnit;
		ctx.enclosingType = record->containingType;
		ctx.primitiveThis = record->primitiveReceiverType; // a primitive receiver (extension method) is a pointer this
		ctx.scope = record->scope; // the function's enclosing namespace/class - unqualified type lookup context

		// The function's scope layer: parameters + locals live here for the whole body.
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
		BindParameterPacks(model, ctx, *record);

		// A lambda's call method: its captures are names bound to the closure's fields (docs/language/07).
		if (const Closure* closure = model.ClosureOf(record->containingType)) EnterClosureBody(ctx, *closure);

		// The root region: a block that is also the scope carrier for the body.
		fn.root = fn.NewNode<BlockNode>(nullptr);
		size_t errorsBefore = errors.GetErrors().Count();
		auto r = LowerFunctionBody(ctx, record, body, fn.root);
		if (r.Ok()) r = ctx.jumps.Validate();
		if (r.Ok()) r = ReturnsOnEveryPath(*record, model.ClosureOf(record->containingType) != nullptr, body, *fn.root);
		if (!r.Ok())
		{
			// Contract: an incomplete lowering ALWAYS reports why. Some paths return a raw Fail without
			// recording it; guarantee this function contributed at least one diagnostic.
			if (errors.GetErrors().Count() == errorsBefore)
				errors.AddError(r.loc.line ? r.loc : (body ? body->startLocation : SourceLocation()), r.message);
			return fn; // incomplete tree, codegen skips it
		}

		MoveAtLastUse(ctx, fn);
		fn.complete = true;
		return fn;
	}
} // namespace Scripting
