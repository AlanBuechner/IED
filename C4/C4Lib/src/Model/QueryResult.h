#pragma once
#include "Core/Core.h"
#include "Lexer/Token.h"  // SourceLocation
#include <utility>

namespace Scripting
{
	/// Errors a semantic query can report. A query never "partially succeeds": the caller sees
	/// exactly one of these (or None) and decides whether to surface it as a diagnostic.
	enum class QueryError
	{
		None,
		NotFound,          // name does not exist in this location's visible scopes
		NotVisible,        // exists but is private/protected from the asking location
		Ambiguous,         // multiple candidates match and none is viable
		NotCallable,       // resolved to a non-function or wrong arity
		NotAValue,         // asked for a value where only a type name (or vice versa) exists
		CyclicDependency,  // type size depends on itself without an indirection layer
		Unsupported,       // feature not implemented yet (message carries the actionable hint)
		ReadOnly           // a method that is not const asked of a read-only object (D-31)
	};

	/// Result of a semantic query: the value, or an error with a human-readable message and the
	/// location where the problem was found. Replaces the old mixed conventions (push a diagnostic
	/// AND return nullptr / warn-and-continue) so every call site handles failure explicitly.
	template <typename T>
	struct QueryResult
	{
		T value{};
		QueryError error = QueryError::None;
		Core::String message;
		SourceLocation loc;
		int32 argument = -1; // a call that fails on one of its arguments: that argument's index

		QueryResult() = default; // explicit: the converting ctor below would otherwise delete it

		/// A bare value is a successful query - so a resolver can `return Value::R(...)` directly.
		QueryResult(const T& v) : value(v) {}
		QueryResult(T&& v) : value(std::move(v)) {}

		bool Ok() const { return error == QueryError::None; }
		explicit operator bool() const { return Ok(); }

		static QueryResult Success(T v)
		{
			QueryResult r;
			r.value = v;
			return r;
		}

		template <typename Msg>
		static QueryResult Fail(QueryError e, const Msg& msg, SourceLocation at = {})
		{
			QueryResult r;
			r.error = e;
			r.message = msg;
			r.loc = at;
			return r;
		}
	};

	/// void specialization: statements report success or an error, never a value.
	template <>
	struct QueryResult<void>
	{
		QueryError error = QueryError::None;
		Core::String message;
		SourceLocation loc;

		bool Ok() const { return error == QueryError::None; }
		explicit operator bool() const { return Ok(); }

		static QueryResult Success() { return QueryResult(); }

		template <typename Msg>
		static QueryResult Fail(QueryError e, const Msg& msg, SourceLocation at = {})
		{
			QueryResult r;
			r.error = e;
			r.message = msg;
			r.loc = at;
			return r;
		}
	};

} // namespace Scripting
