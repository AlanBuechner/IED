#pragma once
#include "Backend/HIR/HIR.h"
#include "Compiler/ErrorTracker.h"

namespace Scripting
{
	class ProgramModel;
	struct FunctionRecord;

	/// The signature every reflection invoke thunk has (D-50): `void(void* code, void* object, void* result, void** arguments)`.
	const FunctionType* InvokeThunkType(ProgramModel& model);

	/// Whether reflection can call `target` through a thunk: not variadic, not local and not an extension of a
	/// primitive.
	bool Invokable(const FunctionRecord& target);

	/// Whether `target` is called through a thunk shared by every function of its signature, which calls the `code` it
	/// is given. A method that dispatches on its object and a foreign (`[link]`) function have thunks of their own.
	bool SharesInvokeThunk(const ProgramModel& model, const FunctionRecord& target);

	/// What a shared thunk depends on, as text: how `this` is passed, `noexcept`, the return and the parameter types.
	/// Functions with the same signature text call through the same thunk.
	Core::String InvokeSignature(const FunctionRecord& target);

	/// The body of an invoke thunk whose own record is `thunk`. A shared thunk calls `code` with `target`'s signature;
	/// any other calls `target` itself and ignores `code`. `object` is the receiver of a method (ignored otherwise),
	/// `arguments[i]` the address of argument i, copied into the call like any by-value argument or bound by a
	/// reference parameter; a virtual method dispatches on `object`. The result is built in `result`, and a reference
	/// result stores the referent's address there. Incomplete (with the reason in `errors`) when an argument cannot be
	/// passed, such as a by-value parameter of a type that cannot be copied.
	HIRFunction LowerInvokeThunk(ProgramModel& model, ErrorTracker& errors, const FunctionRecord& target, const FunctionRecord& thunk, bool shared);
} // namespace Scripting
