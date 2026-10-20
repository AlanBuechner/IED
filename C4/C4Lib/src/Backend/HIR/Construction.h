#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	class ExprLowerer;
	struct BracedInitializerNode;

	/// One argument of a construction: a value, or a nested braced list that initializes the parameter or
	/// field it lands on (`T{ {1, 2}, 3 }`).
	struct ConstructionArgument
	{
		Value value;
		const BracedInitializerNode* brace = nullptr;
	};

	/// What constructing a `T` from arguments runs (docs/language/06, 6.2), shared by `T v(args)`,
	/// `T(args)` / `T{...}` and `new T(args)` / `new T{...}`.
	struct ConstructionPlan
	{
		const FunctionRecord* create = nullptr; // the create to call with `args`; null for aggregate or plain default
		bool aggregate = false;                 // `args` fill the non-static fields in order (T declares no create)
		std::vector<const HIRNode*> args;       // bound to the create's parameters, or to the fields
		const HIRNode* whole = nullptr;         // `T(x)` with x a T: the new object is this value (a copy, or x relocated)
		std::vector<const HIRNode*> prelude;    // evaluated first, in order: aggregate arguments held until the fill
	};

	/// No arguments: the default construction (the user's 0-argument create or the compiler's). Braced
	/// arguments go to a create taking `__init_list<E>` when T has one. Otherwise overload resolution over
	/// T's own creates - creates are not inherited - with defaults filled; one argument that is itself a T
	/// and matches no create copies (or relocates) it; with no create at all the arguments initialize the
	/// fields (aggregate). `args` are the argument values in source order.
	QueryResult<ConstructionPlan> PlanConstruction(LoweringContext& ctx, ExprLowerer& expr, const ObjectType* type,
		const std::vector<ConstructionArgument>& args, bool braced, const AstNode* site);

	/// What a field a braced list or aggregate `T(...)` leaves out starts as: its initializer, evaluated
	/// without an object (it cannot use other fields or `this`), else its default construction, else zero.
	QueryResult<const HIRNode*> OmittedFieldValue(LoweringContext& ctx, const ObjectType* type, const ObjectType::FieldInfo& field);
} // namespace Scripting
