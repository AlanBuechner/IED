#pragma once
#include "Json/JsonValue.h"
#include <string>

namespace Json
{
	/// Serializes an LSP message to its wire form: a `Content-Length` header, blank line, and
	/// UTF-8 JSON body. The length is byte-based so it is correct for multi-byte characters.
	class Writer
	{
	public:
		static std::string Encode(const Value& document);
	};
} // namespace Json
