#pragma once
#include "Core/Core.h"

namespace Scripting
{
	class Type;
	class ObjectType;
	struct ConstantValue;

	/// A type's unique, readable name (D-50): enclosing namespaces and classes, the type's own name and its
	/// template arguments - `Geo::Pair<float, int32>`, `Outer::Inner`, `Box<Geo::Point>*`. Two distinct types
	/// never share one, so reflection can key types by it.
	Core::String QualifiedName(const Type* type);

	/// QualifiedName in symbol characters (letters, digits, `_`): `*` is `P`, `&` is `R`, an extent `A<n>`,
	/// template arguments `T...E`, so `int32*`, `int32&` and `int32[2]` stay distinct in a function's symbol.
	Core::String SymbolToken(const Type* type);

	/// `text` (a QualifiedName) in the symbol characters SymbolToken uses.
	Core::String SymbolTokenOf(const Core::String& text);

	/// The template arguments of an instance as QualifiedName writes them, without the brackets: `int32, Geo::Point`.
	Core::String TemplateArgumentsText(const ObjectType& instance);

	/// `text` with every character that is not a letter or digit written as `_` and two hex digits: distinct texts
	/// never escape alike, so a symbol built from one is unique (`Geo::Point` is `Geo_3a_3aPoint`).
	Core::String EscapedText(const Core::String& text);

	/// A template value argument as names write it: `true`, `-3`, and a float in its shortest exact form with a
	/// point (`2.5`, `1.0`), so distinct values never print alike (BUG-235).
	Core::String ConstantText(const ConstantValue& value);
} // namespace Scripting
