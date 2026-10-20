#include "Model/OperatorTable.h"

namespace Scripting
{
	// The catalog: token -> (binary annotation, unary annotation). A null entry means "not an operator
	// of that arity". Canonical names match the language spec's operator registry exactly, so a method
	// declared [operator("subtract")] is found by QueryOperator("-"), and [operator("[]")] unifies with
	// [operator("index")]. Compound assignments are binary-only. This is the single source of truth for
	// what a token means; both ProgramModel::QueryOperator and HIR dispatch read it.
	struct OperatorEntry
	{
		const char* token;
		const char* binary; // annotation when used between two operands
		const char* unary;  // annotation when used before/after one operand
	};

	static const OperatorEntry kCatalog[] = {
		// arithmetic (spec: + add, - subtract, * multiply, / divide, % modulo)
		{ "+", "add", "unary_plus" },
		{ "-", "subtract", "negate" },
		{ "*", "multiply", "dereference" },
		{ "/", "divide", nullptr },
		{ "%", "modulo", nullptr },
		{ "**", "power", nullptr }, // D-26: built in for numerics
		// comparison (spec: == != < > <= >=)
		{ "==", "equal", nullptr },
		{ "!=", "notequal", nullptr },
		{ "<", "less", nullptr },
		{ ">", "greater", nullptr },
		{ "<=", "less_equal", nullptr },
		{ ">=", "greater_equal", nullptr },
		// logical (structural: primitives use the built-in; objects dispatch to these)
		{ "&&", "logical_and", nullptr },
		{ "||", "logical_or", nullptr },
		{ "!", nullptr, "logical_not" }, // boolean not on an object result
		// bitwise (spec: & | ^ ~ << >>)
		{ "&", "bitwise_and", "address_of" },
		{ "|", "bitwise_or", nullptr },
		{ "^", "bitwise_xor", nullptr },
		{ "~", nullptr, "bitwise_not" },
		{ "<<", "shift_left", nullptr },
		{ ">>", "shift_right", nullptr },
		// indexing (spec: [] is the index operator; the write form []= is its index_assign spelling,
		// mirroring how += spells add_assign - without this entry [operator("[]=")] would store the literal
		// "[]" and no query could find it, so obj[i] = v fell through to the read operator)
		{ "[]", "index", nullptr },
		{ "[]=", "index_assign", nullptr },
		// the call operator (D-23): `x(args)` on an object
		{ "()", "call", nullptr },
		// assignment (docs/language/06 6.4): `=` and `assign` are one operator
		{ "=", "assign", nullptr },
		// compound assignment (in-place form preferred; plain-op fallback is PlainFormFor)
		{ "+=", "add_assign", nullptr },
		{ "-=", "subtract_assign", nullptr },
		{ "*=", "multiply_assign", nullptr },
		{ "/=", "divide_assign", nullptr },
		{ "%=", "modulo_assign", nullptr },
		{ "&=", "and_assign", nullptr },
		{ "|=", "or_assign", nullptr },
		{ "^=", "xor_assign", nullptr },
		{ "<<=", "shift_left_assign", nullptr },
		{ ">>=", "shift_right_assign", nullptr },
		// increments: token is the same for prefix and postfix; HIR passes the text through.
		{ "++", nullptr, "increment" },
		{ "--", nullptr, "decrement" },
	};

	const char* OperatorTable::AnnotationFor(const Core::String& token, bool unary)
	{
		for (const auto& e : kCatalog)
			if (token == e.token)
				return unary ? e.unary : e.binary;
		return nullptr; // not an operator token: the caller treats it as a canonical annotation name
	}

	Core::String OperatorTable::CanonicalAnnotation(const Core::String& asWritten, uint32 parameterCount)
	{
		const bool unary = parameterCount == 0;
		if (const char* mapped = AnnotationFor(asWritten, unary)) return Core::String(mapped);
		if (const char* other = AnnotationFor(asWritten, !unary)) return Core::String(other); // a token with one meaning ("[]", "++")
		return asWritten; // already canonical ("add", "hash", ...)
	}

	bool OperatorTable::IsAssignForm(const Core::String& annotation)
	{
		static const char* kSuffix = "_assign";
		size_t n = 7;
		return annotation.size() > n && annotation.substr(annotation.size() - n) == kSuffix;
	}

	Core::String OperatorTable::PlainFormFor(const Core::String& assignAnnotation)
	{
		if (!IsAssignForm(assignAnnotation)) return assignAnnotation;
		// The plain name is not always the suffix strip: bitwise/logical assigns carry a different
		// prefix than their plain form ("and_assign" -> "bitwise_and", not "and"). Map those explicitly;
		// every other "<op>_assign" strips to "<op>".
		static const char* kBitwiseOrLogical[] = { "and_assign", "or_assign", "xor_assign" };
		static const char* kPlainNames[]       = { "bitwise_and", "bitwise_or", "bitwise_xor" };
		for (int i = 0; i < 3; ++i)
			if (assignAnnotation == kBitwiseOrLogical[i]) return Core::String(kPlainNames[i]);
		return assignAnnotation.substr(0, assignAnnotation.size() - 7);
	}
} // namespace Scripting
