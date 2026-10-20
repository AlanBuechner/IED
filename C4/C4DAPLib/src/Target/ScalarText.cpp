#include "Target/ScalarText.h"
#include <charconv>
#include <cstdio>
#include <cstring>

namespace C4DAP::Target
{
	namespace
	{
		constexpr uint64_t kBitsPerByte = 8;
		constexpr int kAddressDigits = 16;

		uint64_t Unsigned(uint64_t size, const uint8_t* bytes)
		{
			uint64_t value = 0;
			std::memcpy(&value, bytes, size < sizeof(value) ? size : sizeof(value));
			return value;
		}

		int64_t Signed(uint64_t size, const uint8_t* bytes)
		{
			const uint64_t raw = Unsigned(size, bytes);
			if (size >= sizeof(int64_t)) return static_cast<int64_t>(raw);
			const uint64_t sign = uint64_t(1) << (size * kBitsPerByte - 1);
			return static_cast<int64_t>((raw ^ sign) - sign);
		}

		template <typename Float> std::string Shortest(Float value)
		{
			char buffer[64];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			return std::string(buffer, result.ptr);
		}

		std::string CharacterText(uint64_t code)
		{
			std::string text = std::to_string(code);
			if (code >= 0x20 && code < 0x7F) text += std::string(" '") + static_cast<char>(code) + "'";
			return text;
		}
	}

	std::string ScalarText(BasicKind kind, uint64_t size, const uint8_t* bytes)
	{
		switch (kind)
		{
		case BasicKind::Signed: return std::to_string(Signed(size, bytes));
		case BasicKind::Unsigned: return std::to_string(Unsigned(size, bytes));
		case BasicKind::Bool: return Unsigned(size, bytes) != 0 ? "true" : "false";
		case BasicKind::Char8: case BasicKind::Char16: case BasicKind::Char32: return CharacterText(Unsigned(size, bytes));
		case BasicKind::Float:
		{
			if (size == sizeof(float)) { float value; std::memcpy(&value, bytes, sizeof(value)); return Shortest(value); }
			if (size == sizeof(double)) { double value; std::memcpy(&value, bytes, sizeof(value)); return Shortest(value); }
			return "?";
		}
		default: return std::to_string(Unsigned(size, bytes));
		}
	}

	int64_t IntegerValue(BasicKind kind, uint64_t size, const uint8_t* bytes)
	{
		return kind == BasicKind::Signed ? Signed(size, bytes) : static_cast<int64_t>(Unsigned(size, bytes));
	}

	std::string BasicTypeName(BasicKind kind, uint64_t size)
	{
		const std::string bits = std::to_string(size * kBitsPerByte);
		switch (kind)
		{
		case BasicKind::Signed: return "int" + bits;
		case BasicKind::Unsigned: return "uint" + bits;
		case BasicKind::Float: return size == sizeof(float) ? "float" : "double";
		case BasicKind::Bool: return "bool";
		case BasicKind::Char8: return "char8";
		case BasicKind::Char16: return "char16";
		case BasicKind::Char32: return "char32";
		default: return "byte" + bits;
		}
	}

	std::string AddressText(uint64_t address)
	{
		char buffer[2 + kAddressDigits + 1];
		std::snprintf(buffer, sizeof(buffer), "0x%016llx", static_cast<unsigned long long>(address));
		return buffer;
	}

	std::string QuotedText(const std::string& text, bool complete)
	{
		std::string quoted = "\"";
		for (char c : text)
		{
			switch (c)
			{
			case '"': quoted += "\\\""; break;
			case '\\': quoted += "\\\\"; break;
			case '\n': quoted += "\\n"; break;
			case '\r': quoted += "\\r"; break;
			case '\t': quoted += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					char escape[8];
					std::snprintf(escape, sizeof(escape), "\\x%02x", static_cast<unsigned char>(c));
					quoted += escape;
				}
				else quoted += c;
			}
		}
		quoted += "\"";
		if (!complete) quoted += "...";
		return quoted;
	}
} // namespace C4DAP::Target
