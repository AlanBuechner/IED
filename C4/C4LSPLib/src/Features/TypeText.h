#pragma once
#include "Core/Core.h"

namespace Scripting { class Type; class FunctionType; }

namespace C4LSP::Features
{
	/// Render a resolved semantic type as the short text shown in hover and completion
	/// (e.g. "int32", "Point*", "void Add(int32, float)"). Falls back to "<unknown>" for null.
	Core::String TypeText(const Scripting::Type* type);

	/// The type a type name denotes, past pointers, arrays, references and aliases: `char` for `const char*`.
	const Scripting::Type* NamedType(const Scripting::Type* type);

	/// Render a function/method signature from its semantic type (returns the bare type name
	/// when `type` is not a function).
	Core::String FunctionSignature(const Scripting::FunctionType* func);
} // namespace C4LSP::Features
