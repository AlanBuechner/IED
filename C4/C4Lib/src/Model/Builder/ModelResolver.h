#pragma once
#include "Model/Builder/ModelBuilder.h"

namespace Scripting
{
	// =====================================================================
	// MODEL RESOLVER (doc 02, PASS 2) - the fixed-point resolution phase. Pass 1 (ModelBuilder) has
	// registered every shell; now that a name always resolves to something registered, this walks the
	// builder's pending work-lists and settles them: field types, base classes, method signatures,
	// enum values, alias targets, free functions (qualified + unqualified), out-of-line bodies, and
	// constant folding - iterating until nothing changes. A name resolves because the thing it names
	// was registered in pass 1, regardless of where it was written: declaration ORDER NEVER MATTERS.
	// After convergence it also runs the ONE body-opening pass (GAP A): instantiate every template
	// specialization a function body references, so those types are concrete and queryable before any
	// lower system runs. It is friended into ModelBuilder to read the pending work-lists and report.
	// =====================================================================
	class ModelResolver
	{
	public:
		/// One fixed-point round over every pending list; true when anything changed this round.
		bool ResolveRound(ModelBuilder& builder, ModelBuilder::Context& ctx);

		/// GAP A (post-resolution): pass 2 resolves only DECLARATION-level types, so a `Vec<int32> v;`
		/// inside a body is never resolved by it and its specialization would stay uninstantiated.
		/// This walks every function body once, resolving each template-specialization type it names -
		/// the ONLY place the model opens a body.
		void InstantiateBodyReferences(ModelBuilder& builder, ModelBuilder::Context& ctx);

	private:
		friend class ModelBuilder; // drives the rounds + reports what stays pending

		bool ResolveVariables(ModelBuilder& builder, ModelBuilder::Context& ctx);
		bool ResolveEnums(ModelBuilder& builder, ModelBuilder::Context& ctx);
		/// The values of `e`'s members in order; false, with the reason in `e.why`, when one does not fold yet.
		static bool MemberValues(const ProgramModel& model, ModelBuilder::PendingEnum& e, Core::Vector<int64>& values);
		static QueryResult<void> AttachBase(const ObjectType& derived, const Type* base);
		static void ReportMembersOutOfRange(ModelBuilder& builder, const ModelBuilder::Context& ctx, const ModelBuilder::PendingEnum& e,
			const Core::Vector<int64>& values);
		bool ResolveAliases(ModelBuilder& builder, ModelBuilder::Context& ctx);
		bool ResolveUnions(ModelBuilder& builder, ModelBuilder::Context& ctx);
		bool ResolveObjectMembers(ModelBuilder& builder, ModelBuilder::Context& ctx);   // fields + bases + in-class method decls
		// Free functions (unqualified + qualified namespace/class names) live in the
		// FreeFunctionResolver subsystem; ResolveRound delegates to it.
		// (constant folding moved to ConstEvaluator::FoldGlobalConstants - it is constant evaluation)
	};
} // namespace Scripting
