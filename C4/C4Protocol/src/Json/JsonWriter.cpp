#include "Json/JsonWriter.h"

namespace Json
{
	std::string Writer::Encode(const Value& document)
	{
		std::string body = document.Serialize();
		return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
	}
} // namespace Json
