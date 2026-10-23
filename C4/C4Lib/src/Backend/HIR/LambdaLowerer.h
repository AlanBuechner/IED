#pragma once
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/Construction.h"
#include "Backend/HIR/LambdaCaptures.h"
#include "Model/Closures.h"

namespace Scripting
{
	class ExprLowerer;

	/// A lambda expression (docs/language/07, D-22): its captures are planned against the enclosing scope, the
	/// model builds its closure type, its body lowers as the closure's call method (next to the function that
	/// writes it), and its value is a closure object holding the captures.
	class LambdaLowerer
	{
	public:
		LambdaLowerer(LoweringContext& ctx, ExprLowerer& expr) : m_Ctx(ctx), m_Expr(expr) {}

		QueryResult<Value> Lower(const LambdaNode& lambda);

	private:
		struct PlannedCapture
		{
			ClosureCapture capture;
			ConstructionArgument init; // the value the closure field starts from
		};
		using Plan = std::vector<PlannedCapture>;

		QueryResult<void> Signature(const LambdaNode& lambda, ClosureSpec& spec);
		QueryResult<Plan> PlanCaptures(const LambdaNode& lambda);
		QueryResult<void> PlanListed(const LambdaNode& lambda, Plan& plan);
		QueryResult<void> PlanDefaults(const LambdaNode& lambda, const OuterUses& uses, Plan& plan);
		QueryResult<PlannedCapture> CaptureVariable(const Core::String& name, bool byReference);
		QueryResult<PlannedCapture> CaptureInit(const LambdaCapture& capture);
		QueryResult<PlannedCapture> CaptureThis(bool copy);
		bool UsesMember(const OuterUses& uses) const;
		bool HasThis() const;
		QueryResult<void> LowerCallMethod(const Closure& closure);
		Core::String OwnerName() const;

		LoweringContext& m_Ctx;
		ExprLowerer& m_Expr;
	};

	/// Sets up the lowering of a closure's call method: each capture is a name bound to its closure field, and a
	/// captured `this` or `*this` is what the body's `this` and members refer to.
	void EnterClosureBody(LoweringContext& ctx, const Closure& closure);
}
