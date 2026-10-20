#pragma once
#include "Core/Core.h"

namespace Scripting
{
	/// The ONE operator catalog (doc 08): the data-driven token -> canonical-annotation map, aligned
	/// with the language spec's operator registry (docs/language/06-lifecycle-and-operators.md).
	/// ProgramModel::QueryOperator and HIR both read from here, so the two phases cannot disagree about
	/// what a token means. Canonical names are exactly what users write in [operator("...")]: "add",
	/// "subtract", "bitwise_and", "shift_left", "index", ... A few operators have NO token (lifecycle):
	/// "create", "delete", "copy", "move", "hash", ... - those pass through unchanged.
	class OperatorTable
	{
	public:
		/// The canonical annotation a source token selects for the given arity, or nullptr when the
		/// token is not an operator of that arity (the caller then treats it as already-canonical).
		/// Arity matters because some tokens are context-sensitive: "*" is multiply (binary) vs
		/// dereference (unary); "&" is bitwise_and (binary) vs address_of (unary); "-" is subtract vs negate.
		static const char* AnnotationFor(const Core::String& token, bool unary);

		/// Normalize an as-written [operator("...")] name to its canonical form (D-24): a known operator TOKEN
		/// maps to its canonical name for the method's arity - `"-"` on a method with no parameter is
		/// `negate`, with one it is `subtract` - and anything already canonical ("add", "hash") passes through
		/// unchanged. Collection and query both run through the catalog, so every spelling of the same
		/// operator unifies to one canonical string.
		static Core::String CanonicalAnnotation(const Core::String& asWritten, uint32 parameterCount);

		/// True when the annotation is an in-place "<op>_assign" form.
		static bool IsAssignForm(const Core::String& annotation);

		/// The plain form behind a compound-assign annotation, for the fallback (use the plain operator
		/// when no in-place method exists): "add_assign" -> "add", "and_assign" -> "bitwise_and". Not a
		/// simple suffix strip - bitwise/logical assigns map to their bitwise_/logical_ plain names.
		static Core::String PlainFormFor(const Core::String& assignAnnotation);
	};
} // namespace Scripting
