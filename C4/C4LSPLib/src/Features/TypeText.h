#pragma once
#include "Core/Core.h"

namespace C4Compiler { class Type; class FunctionType; struct AstNode; }

namespace C4LSP::Features
{
	/// Render a resolved semantic type as the short text shown in hover and completion
	/// (e.g. "int32", "Point*", "void Add(int32, float)"). Falls back to "<unknown>" for null.
	Core::String TypeText(const C4Compiler::Type* type);

	/// The type a type name denotes, past pointers, arrays, references and aliases: `char` for `const char*`.
	const C4Compiler::Type* NamedType(const C4Compiler::Type* type);

	/// Where the object, enum or union type a value of `type` names is declared (NamedType first); null for any other.
	const C4Compiler::AstNode* DeclarationOfNamedType(const C4Compiler::Type* type);

	/// Render a function/method signature from its semantic type (returns the bare type name
	/// when `type` is not a function).
	Core::String FunctionSignature(const C4Compiler::FunctionType* func);
} // namespace C4LSP::Features
