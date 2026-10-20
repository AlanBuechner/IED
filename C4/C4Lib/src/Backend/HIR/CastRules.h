#pragma once
#include "Model/Types/Type.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"

namespace Scripting::CastRules
{
	/// Why the named cast `kind` cannot turn a `from` into a `to` (D-12), or empty when it can:
	/// - static_cast: numbers (and enums, bools, characters) to numbers; pointers between related objects, to or
	///   from `void*`, never dropping const; an object to one of its bases (a copy of that part); packed vectors of one
	///   lane count lane by lane (a pure packed vector object counts as its lanes), and a number into every lane of one
	///   (D-62; a C-style cast too).
	/// - reinterpret_cast: a pointer to any pointer, and between a pointer and an integer, never dropping const; a
	///   packed vector to another of the same size, and a number to another number of the same size, keeping the bits.
	/// - const_cast: only adds or removes const, at any pointer level.
	/// A C-style cast and dynamic_cast are not checked here (dynamic_cast is the checked cast).
	Core::String WhyRejected(CastExpressionNode::CastKind kind, const Type* from, const Type* to);

	/// Whether the cast keeps its operand's bits instead of converting its value: a reinterpret_cast between two
	/// numbers (D-64) or two packed vectors (D-62).
	bool KeepsBits(CastExpressionNode::CastKind kind, const Type* from, const Type* to);
}
