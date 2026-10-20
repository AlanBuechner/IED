#pragma once

namespace Scripting
{
	/// Canonical type representation for semantic analysis.
	enum class TypeKind
	{
		Void,
		Bool,
		Int8, Int16, Int32, Int64,
		UInt8, UInt16, UInt32, UInt64,
		SNorm8, SNorm16, SNorm32, SNorm64,   // normalized signed: value in [-1, 1], stored as intN
		UNorm8, UNorm16, UNorm32, UNorm64,   // normalized unsigned: value in [0, 1], stored as uintN
		Float32, Float64,
		Char8,      // byte character (UTF-8 code unit); `char` is its built-in alias
		Char16,     // UTF-16 code unit (uint16) - Windows API / UTF-16 support
		Char32,     // UTF-32 code point (uint32) - full Unicode code points
		Object,
		Enum,
		Union,
		Swizzle,      // swizzle type: an N-lane SIMD vector with named single-char lanes (packed <N x T> / unpacked {T,...})
		Alias,
		Pointer,
		Reference,
		Array,
		FunPtr,
		Func,         // func<R(A)>: a callable value {code, env} (docs/language/07)
		Function,
		InitList,	// designated-init maps (.x = 1) may need their own kind; see WORK.md __init_map item
		TemplateName, // a function template's name as an argument, before a funcptr/func parameter deduces it
		BracedList,   // a braced argument `{a, b}` while a call is resolved: only its entry count (D-57)
		Invalid
	};
} // namespace Scripting
