#pragma once
#include "Model/DeclaredAnnotations.h"

namespace Scripting
{
	/// How a folded argument fits an annotation parameter (docs/language/12 section 4.1).
	struct ArgumentFit
	{
		bool fits = false;
		bool converted = false;  // an integer for a float: it counts against the overload (section 4.3)
		ReflectedArgument value; // as the parameter takes it
		Core::String why;        // when it does not fit: `"far" is a string`
	};

	/// Whether `argument`, written as `written` (its expression), fits `parameter`, and as what.
	ArgumentFit FitArgument(const ReflectedArgument& argument, const AstNode* written, const AnnotationParameter& parameter);

	/// The identifiers an expression or type names, in order: `Render::Pass::Glass` of a static access or a type name, `Glass`
	/// of an identifier; empty for anything else.
	Core::Vector<Core::String> WrittenNameSegments(const AstNode* written);
} // namespace Scripting
