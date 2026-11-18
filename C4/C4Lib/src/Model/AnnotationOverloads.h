#pragma once
#include "Model/DeclaredAnnotations.h"
#include "Model/QueryResult.h"

namespace Scripting
{
	struct AnnotationItem;

	/// An annotation use matched to the declaration it takes (docs/language/12 section 4.3).
	struct AnnotationMatch
	{
		const AnnotationSignature* signature = nullptr;
		Core::Vector<ReflectedArgument> arguments; // as its parameters take them, its defaults filled in
	};

	/// The overload of `overloads` that the use `item`, written on `place` with its arguments folded to `folded`, takes: one
	/// that goes there and that its arguments fit, the fewest conversions winning, then one without a pack. A failure says why,
	/// where the use or the argument that does not fit is written.
	QueryResult<AnnotationMatch> ChooseAnnotationOverload(const DeclaredAnnotations::Overloads& overloads, const AnnotationItem& item,
		AnnotationPlace place, const Core::Vector<ReflectedArgument>& folded);

	/// The overload a use with `arguments` arguments most likely means as it is typed: the first that takes that many, else
	/// the first. Null for no overloads.
	const AnnotationSignature* LikelyOverload(const DeclaredAnnotations::Overloads& overloads, uint32 arguments);
	/// The overload the use `item` most likely takes, read from its text alone (an editor's go-to-definition): the first that
	/// takes its argument count and whose parameters each written literal fits (a string literal a string, a number a
	/// number, `true` a bool), else the first that takes that many.
	const AnnotationSignature* LikelyOverload(const DeclaredAnnotations::Overloads& overloads, const AnnotationItem& item);
} // namespace Scripting
