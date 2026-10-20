#include "JsonObjectDescription.h"
#include <fstream>
#include <charconv>
#include <cstdio>

namespace Core
{
	// --------------------------------- Recursive-Descent JSON Parser --------------------------------- //

	struct JsonSyntaxError
	{
		const char* message;
		size_t position;
	};

	struct JsonParser
	{
		std::string_view input;
		size_t position = 0;

		void Require(bool condition, const char* message) const
		{
			if (!condition)
				throw JsonSyntaxError{ message, position };
		}

		inline char CurrentChar() const
		{
			Require(position < input.size(), "JSON parser reached end of input unexpectedly");
			return input[position];
		}

		inline void Advance()
		{
			++position;
		}

		inline bool AtEnd() const
		{
			return position >= input.size();
		}

		void SkipWhitespace()
		{
			while (!AtEnd())
			{
				char c = input[position];
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
					Advance();
				else
					break;
			}
		}

		ObjectDescription ParseValue()
		{
			SkipWhitespace();
			Require(!AtEnd(), "Unexpected end of JSON input");

			char c = CurrentChar();

			if (c == '"')
				return ParseString();
			else if (c == '{')
				return ParseObject();
			else if (c == '[')
				return ParseArray();
			else if (c == 't' || c == 'f')
				return ParseBool();
			else if (c == 'n')
				return ParseNull();
			else if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
				return ParseNumber();

			Require(false, "Unexpected character in JSON value");
			return ObjectDescription();
		}

		ObjectDescription ParseString()
		{
			SkipWhitespace();
			Require(CurrentChar() == '"', "Expected opening quote for string");
			Advance(); // skip opening '"'

			Core::String result;

			while (!AtEnd())
			{
				char c = CurrentChar();

				if (c == '"')
				{
					Advance(); // skip closing '"'
					return ObjectDescription::CreateFrom(result);
				}

				if (c == '\\')
				{
					Advance(); // skip backslash
					Require(!AtEnd(), "Unexpected end of JSON string escape");
					char escaped = CurrentChar();
					Advance();

					switch (escaped)
					{
					case '"':  result += '"'; break;
					case '\\': result += '\\'; break;
					case '/':  result += '/'; break;
					case 'b':  result += '\b'; break;
					case 'f':  result += '\f'; break;
					case 'n':  result += '\n'; break;
					case 'r':  result += '\r'; break;
					case 't':  result += '\t'; break;
					case 'u':
					{
						// Parse 4 hex digits for Unicode escape \uXXXX
						Require(!AtEnd() && position + 3 < input.size(), "Invalid unicode escape in JSON string");
						std::string_view hexStr = input.substr(position, 4);
						uint32 codepoint = 0;

						for (int i = 0; i < 4; ++i)
						{
							char h = static_cast<char>(hexStr[i]);
							codepoint <<= 4;
							if (h >= '0' && h <= '9')
								codepoint |= static_cast<uint32>(h - '0');
							else if (h >= 'a' && h <= 'f')
								codepoint |= static_cast<uint32>(h - 'a' + 10);
							else if (h >= 'A' && h <= 'F')
								codepoint |= static_cast<uint32>(h - 'A' + 10);
							else
							{
								Require(false, "Invalid hex digit in unicode escape");
							}
						}
						position += 4;

						// Handle surrogate pairs: \uD800-\uDBFF followed by \uDC00-\uDFFF
						if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
						{
							Require(!AtEnd() && input[position] == '\\' && position + 1 < input.size() && input[position + 1] == 'u', "High surrogate without low surrogate in JSON string");
							position += 2; // skip \u
							Require(position + 3 < input.size(), "Invalid unicode escape in JSON string");

							std::string_view hexStr2 = input.substr(position, 4);
							uint32 low = 0;
							for (int i = 0; i < 4; ++i)
							{
								char h = static_cast<char>(hexStr2[i]);
								low <<= 4;
								if (h >= '0' && h <= '9')
									low |= static_cast<uint32>(h - '0');
								else if (h >= 'a' && h <= 'f')
									low |= static_cast<uint32>(h - 'a' + 10);
								else if (h >= 'A' && h <= 'F')
									low |= static_cast<uint32>(h - 'A' + 10);
								else
									Require(false, "Invalid hex digit in unicode escape");
							}
							position += 4;

							Require(low >= 0xDC00 && low <= 0xDFFF, "Invalid low surrogate in JSON string");

							// Combine surrogates into a single codepoint
							codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
						}

						// Encode as UTF-8
						if (codepoint <= 0x7F)
						{
							result += static_cast<char>(codepoint);
						}
						else if (codepoint <= 0x7FF)
						{
							result += static_cast<char>(0xC0 | (codepoint >> 6));
							result += static_cast<char>(0x80 | (codepoint & 0x3F));
						}
						else if (codepoint <= 0xFFFF)
						{
							result += static_cast<char>(0xE0 | (codepoint >> 12));
							result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
							result += static_cast<char>(0x80 | (codepoint & 0x3F));
						}
						else if (codepoint <= 0x10FFFF)
						{
							result += static_cast<char>(0xF0 | (codepoint >> 18));
							result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
							result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
							result += static_cast<char>(0x80 | (codepoint & 0x3F));
						}
						else
						{
							Require(false, "Codepoint out of range in JSON unicode escape");
						}
						break;
					}
					default:
						Require(false, "Invalid escape sequence in JSON string");
						break;
					}
				}
				else
				{
					result += c;
					Advance();
				}
			}

			Require(false, "Unterminated JSON string");
			return ObjectDescription::CreateFrom(result);
		}

		ObjectDescription ParseNumber()
		{
			SkipWhitespace();
			size_t start = position;

			// Optional leading minus
			if (CurrentChar() == '-')
				Advance();

			// Integer part
			if (CurrentChar() == '0')
			{
				Advance();
			}
			else if (std::isdigit(static_cast<unsigned char>(CurrentChar())))
			{
				while (!AtEnd() && std::isdigit(static_cast<unsigned char>(CurrentChar())))
					Advance();
			}
			else
			{
				Require(false, "Invalid number in JSON");
			}

			bool isFloat = false;

			// Fractional part
			if (!AtEnd() && CurrentChar() == '.')
			{
				isFloat = true;
				Advance();
				while (!AtEnd() && std::isdigit(static_cast<unsigned char>(CurrentChar())))
					Advance();
			}

			// Exponent part
			if (!AtEnd() && (CurrentChar() == 'e' || CurrentChar() == 'E'))
			{
				isFloat = true;
				Advance();
				if (!AtEnd() && (CurrentChar() == '+' || CurrentChar() == '-'))
					Advance();
				while (!AtEnd() && std::isdigit(static_cast<unsigned char>(CurrentChar())))
					Advance();
			}

			std::string_view numStr = input.substr(start, position - start);

			if (isFloat)
			{
				double value;
				auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), value);
				Require(ec == std::errc(), "Failed to parse JSON float");
				return ObjectDescription::CreateFrom(value);
			}

			// Integer: try signed first, then unsigned
			// Check if the number was negative
			bool negative = (numStr[0] == '-');

			if (negative)
			{
				int64 value;
				auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), value);
				Require(ec == std::errc(), "Failed to parse JSON signed integer");
				return ObjectDescription::CreateFrom(value);
			}
			else
			{
				uint64 value;
				auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), value);
				Require(ec == std::errc(), "Failed to parse JSON unsigned integer");
				return ObjectDescription::CreateFrom(value);
			}
		}

		ObjectDescription ParseArray()
		{
			SkipWhitespace();
			Require(CurrentChar() == '[', "Expected '[' for JSON array");
			Advance(); // skip '['

			ObjectDescription desc(ObjectDescription::Type::Array);

			SkipWhitespace();
			if (!AtEnd() && CurrentChar() == ']')
			{
				Advance(); // skip ']'
				return desc;
			}

			// First element
			desc.GetAsObjectArray().Push(ParseValue());

			SkipWhitespace();
			while (!AtEnd() && CurrentChar() == ',')
			{
				Advance(); // skip ','
				desc.GetAsObjectArray().Push(ParseValue());
				SkipWhitespace();
			}

			Require(!AtEnd() && CurrentChar() == ']', "Expected ']' to close JSON array");
			Advance(); // skip ']'

			return desc;
		}

		ObjectDescription ParseObject()
		{
			SkipWhitespace();
			Require(CurrentChar() == '{', "Expected '{' for JSON object");
			Advance(); // skip '{'

			ObjectDescription desc(ObjectDescription::Type::Object);

			SkipWhitespace();
			if (!AtEnd() && CurrentChar() == '}')
			{
				Advance(); // skip '}'
				return desc;
			}

			// First key-value pair
			Core::String key = ParseString().GetAsString();
			SkipWhitespace();

			Require(!AtEnd() && CurrentChar() == ':', "Expected ':' after JSON object key");
			Advance(); // skip ':'

			desc.GetAsDescriptionMap()[key] = ParseValue();

			SkipWhitespace();
			while (!AtEnd() && CurrentChar() == ',')
			{
				Advance(); // skip ','
				key = ParseString().GetAsString();
				SkipWhitespace();

				Require(!AtEnd() && CurrentChar() == ':', "Expected ':' after JSON object key");
				Advance(); // skip ':'

				desc.GetAsDescriptionMap()[key] = ParseValue();

				SkipWhitespace();
			}

			Require(!AtEnd() && CurrentChar() == '}', "Expected '}' to close JSON object");
			Advance(); // skip '}'

			return desc;
		}

		ObjectDescription ParseBool()
		{
			SkipWhitespace();
			if (input.substr(position, 4) == "true")
			{
				position += 4;
				return ObjectDescription::CreateFrom(true);
			}
			else if (input.substr(position, 5) == "false")
			{
				position += 5;
				return ObjectDescription::CreateFrom(false);
			}

			Require(false, "Invalid JSON boolean value");
			return ObjectDescription();
		}

		ObjectDescription ParseNull()
		{
			SkipWhitespace();
			if (input.substr(position, 4) == "null")
			{
				position += 4;
				return ObjectDescription(ObjectDescription::Type::Null);
			}

			Require(false, "Invalid JSON null value");
			return ObjectDescription(ObjectDescription::Type::Null);
		}
	};

	static ObjectDescription LoadObjectDescriptionFromJsonStringView(std::string_view json)
	{
		JsonParser parser;
		parser.input = json;
		try
		{
			return parser.ParseValue();
		}
		catch (const JsonSyntaxError& error)
		{
			CORE_ASSERT(false, error.message, "");
			return ObjectDescription();
		}
	}

	bool TryLoadObjectDescriptionFromJsonString(const std::string& json, ObjectDescription& out, std::string* error)
	{
		JsonParser parser;
		parser.input = json;
		try
		{
			ObjectDescription value = parser.ParseValue();
			parser.SkipWhitespace();
			parser.Require(parser.AtEnd(), "Unexpected text after the JSON value");
			out = std::move(value);
			return true;
		}
		catch (const JsonSyntaxError& syntax)
		{
			if (error)
				*error = std::string(syntax.message) + " at offset " + std::to_string(syntax.position);
			return false;
		}
	}

	ObjectDescription LoadObjectDescriptionFromJsonString(const std::string& str)
	{
		return LoadObjectDescriptionFromJsonStringView(str);
	}

	ObjectDescription LoadObjectDescriptionFromJsonFile(const fs::path& file)
	{
		std::ifstream f(file, std::ios::binary);
		CORE_ASSERT(f.is_open(), "Failed to open JSON file", "");
		std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		return LoadObjectDescriptionFromJsonStringView(content);
	}

	// --------------------------------- JSON Serialization (recursive string builder) --------------------------------- //

	static void AppendJsonStringEscape(const std::string& str, std::string& out)
	{
		out.push_back('"');
		for (char c : str)
		{
			switch (c)
			{
				case '\\': out += "\\\\"; break;
				case '"':  out += "\\\""; break;
				case '\b': out += "\\b"; break;
				case '\f': out += "\\f"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					// Control characters (U+0000 through U+001F) must be escaped
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buf[7];
						std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
						out += buf;
					}
					else
					{
						out.push_back(c);
					}
					break;
			}
		}
		out.push_back('"');
	}

	static void AppendJsonValue(const ObjectDescription& desc, std::string& out, int /*indent*/)
	{
		switch (desc.GetType())
		{
			case ObjectDescription::Type::Null:
				out += "null";
				break;

			case ObjectDescription::Type::String:
				AppendJsonStringEscape(desc.GetAsString(), out);
				break;

			case ObjectDescription::Type::Array:
			{
				out.push_back('[');
				const auto& array = desc.GetAsObjectArray();
				for (uint32 i = 0; i < array.Count(); i++)
				{
					if (i > 0)
						out.push_back(',');
					AppendJsonValue(array[i], out, /*indent*/0);
				}
				out.push_back(']');
				break;
			}

			case ObjectDescription::Type::Object:
			{
				out.push_back('{');
				const auto& entries = desc.GetAsDescriptionMap();
				bool first = true;
				for (const auto& entry : entries)
				{
					if (!first)
						out.push_back(',');
					first = false;
					AppendJsonStringEscape(entry.first, out);
					out.push_back(':');
					AppendJsonValue(entry.second, out, /*indent*/0);
				}
				out.push_back('}');
				break;
			}

			case ObjectDescription::Type::Uint:
				out += std::to_string(desc.GetAsUint());
				break;

			case ObjectDescription::Type::Sint:
				out += std::to_string(desc.GetAsInt());
				break;

			case ObjectDescription::Type::Float:
			{
				char buf[64];
				auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), desc.GetAsFloat(), std::chars_format::general);
				if (ec == std::errc() && ptr > buf)
					out.append(buf, static_cast<size_t>(ptr - buf));
				else
				{
					// Fallback: use snprintf for full precision
					std::snprintf(buf, sizeof(buf), "%.17g", desc.GetAsFloat());
					out += buf;
				}
				break;
			}

			case ObjectDescription::Type::Bool:
				out += desc.GetAsBool() ? "true" : "false";
				break;

			default:
				CORE_ASSERT(false, "Unknown ObjectDescription type during JSON serialization", "");
				break;
		}
	}

	std::string SaveObjectDescriptionToJson(const ObjectDescription& desc)
	{
		std::string result;
		AppendJsonValue(desc, result, /*indent*/0);
		return result;
	}
}
