#pragma once
#include "Core/Core.h"
#include <string>

namespace Scripting
{
	/// A folded compile-time constant (doc 04: QueryConstant returns one of these).
	/// Order-independent: a const may name another const, an enum member, or sizeof(T);
	/// the evaluator folds to a fixpoint with cycle detection.
	struct ConstantValue
	{
		enum class Kind { None, Int64, Float64, Bool, String, Guid };

		Kind kind = Kind::None;
		int64 intValue = 0;
		double floatValue = 0.0;
		bool booleanValue = false;
		std::string stringValue; // Kind::String: the code units (unitWidth little-endian bytes each), no terminator
		uint8 unitWidth = 1;     // Kind::String: 1 char8, 2 char16, 4 char32
		unsigned char guidBytes[16]; // Kind::Guid: the 16 _GUID bytes in Win32 memory layout (Data1 LE, Data2 LE, Data3 LE, Data4)

		bool IsNone() const { return kind == Kind::None; }

		static ConstantValue Int(int64 v) { ConstantValue c; c.kind = Kind::Int64; c.intValue = v; return c; }
		static ConstantValue Float(double v) { ConstantValue c; c.kind = Kind::Float64; c.floatValue = v; return c; }
		static ConstantValue Bool(bool v) { ConstantValue c; c.kind = Kind::Bool; c.booleanValue = v; return c; }
		static ConstantValue String(std::string units, uint8 unitWidth = 1) { ConstantValue c; c.kind = Kind::String; c.stringValue = std::move(units); c.unitWidth = unitWidth; return c; }
		static ConstantValue Guid(const unsigned char b[16]) { ConstantValue c; c.kind = Kind::Guid; for (int i = 0; i < 16; ++i) c.guidBytes[i] = b[i]; return c; }

		/// The integer view of the value (floats truncate, bool maps to 0/1). None yields 0.
		int64 AsInt() const
		{
			switch (kind) {
			case Kind::Int64: return intValue;
			case Kind::Float64: return static_cast<int64>(floatValue);
			case Kind::Bool: return booleanValue ? 1 : 0;
			default: return 0;
			}
		}
	};
} // namespace Scripting
