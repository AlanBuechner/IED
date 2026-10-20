#pragma once
#include "Target/Symbols.h"
#include <cstdint>
#include <string>

namespace C4DAP::Target
{
	/// A basic value's bytes as the debugger shows them: integers (bytes too) in decimal, floats in their shortest exact form,
	/// `true` / `false`, characters as their code and, when printable, the character ('a' is "97 'a'").
	std::string ScalarText(BasicKind kind, uint64_t size, const uint8_t* bytes);

	/// An integer's bytes as a number: sign-extended for a Signed kind, zero-extended for the others.
	int64_t IntegerValue(BasicKind kind, uint64_t size, const uint8_t* bytes);

	/// The C4 spelling of a basic type: int32, uint8, float, double, bool, char8, ...
	std::string BasicTypeName(BasicKind kind, uint64_t size);

	/// An address as the debugger shows it: 0x and 16 hex digits.
	std::string AddressText(uint64_t address);

	/// `text` as a C4 string literal (quotes, escapes for control characters, quotes and backslashes), with
	/// `...` after the closing quote when the string went on.
	std::string QuotedText(const std::string& text, bool complete);
} // namespace C4DAP::Target
