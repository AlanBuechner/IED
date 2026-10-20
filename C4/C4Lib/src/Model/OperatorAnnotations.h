#pragma once
#include "Model/QueryResult.h"
#include "Model/SymbolTable.h"
#include "Core/Core.h"

namespace Scripting
{
	class AnnotationNode;

	/// Collect the [operator("...")] names from one annotation node into `out` (doc 08). A method may
	/// carry SEVERAL: `[operator("create","copy")]` or chained `[operator("add")] [operator("sub")]`.
	/// Both QueryOperator (annotation -> candidates) and QueryFunction ("operator_add" name ->
	/// annotation "add") read the resulting set. Shared by the builder's pass-2 registration AND the
	/// template-instantiation path, so an instantiated method carries exactly the operator annotations
	/// its hand-written twin would - operators on `Vec<int32>` resolve identically to those on a class
	/// written directly.
	/// A token with a unary and a binary meaning binds by `parameterCount`, the method's explicit parameters (D-24).
	void CollectOperatorAnnotations(const AnnotationNode* ann, uint32 parameterCount, Core::Vector<Core::String>& out);

	/// The lifecycle operators of a method (docs/language/06): `delete` takes no parameter, `copy` takes one
	/// reference to its own type; `"destroy"` and `"finalizer"` (D-03) and `"move"` (D-05) are errors that
	/// name the replacement.
	QueryResult<void> CheckLifecycleOperators(const FunctionRecord& rec);

	/// Collect the [group("name")] annotations from one annotation node into `out` (reflection, plan 14).
	/// Groups are fully defined by user code - there are no default groups. A declaration may carry
	/// several: [group("components")] [group("SubSystem")]. Shared by every registration path so an
	/// instantiated/template type carries the same groups its hand-written twin would.
	void CollectGroupAnnotations(const AnnotationNode* ann, Core::Vector<Core::String>& out);
} // namespace Scripting
