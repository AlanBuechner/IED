#include "Json/JsonReader.h"
#include <cctype>

namespace Json
{
	namespace
	{
		constexpr size_t kBlankLineBytes = 4; // "\r\n\r\n"
	}

	void Reader::Append(const char* data, size_t length)
	{
		m_Buffer.append(data, length);
	}

	bool Reader::TryReadDocument(Value& outDocument)
	{
		while (true)
		{
			const size_t headerEnd = FindHeaderEnd();
			if (headerEnd == npos)
				return false;

			const size_t bodyStart = headerEnd + kBlankLineBytes;
			size_t contentLength = 0;
			if (!ParseContentLength(headerEnd, contentLength))
			{
				m_Buffer.erase(0, bodyStart); // a header with no usable length: drop it and read on
				continue;
			}
			if (m_Buffer.size() < bodyStart + contentLength)
				return false; // body not fully received yet

			const std::string body = m_Buffer.substr(bodyStart, contentLength);
			m_Buffer.erase(0, bodyStart + contentLength);
			Core::ObjectDescription parsed;
			if (Core::TryLoadObjectDescriptionFromJsonString(body, parsed))
			{
				outDocument = Value(parsed);
				return true;
			}
			// a body that is not JSON: the frame is dropped and reading goes on
		}
	}

	size_t Reader::FindHeaderEnd() const
	{
		return m_Buffer.find("\r\n\r\n");
	}

	bool Reader::ParseContentLength(size_t headerEnd, size_t& outLength) const
	{
		size_t pos = 0;
		while (pos < headerEnd)
		{
			size_t lineEnd = m_Buffer.find('\n', pos);
			if (lineEnd == npos || lineEnd > headerEnd)
				lineEnd = headerEnd;

			std::string line = m_Buffer.substr(pos, lineEnd - pos);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			const size_t colon = line.find(':');
			if (colon != npos)
			{
				std::string key = Trim(line.substr(0, colon));
				for (char& c : key)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (key == "content-length")
					return ParseLength(Trim(line.substr(colon + 1)), outLength);
			}
			pos = lineEnd + 1;
		}
		return false;
	}

	bool Reader::ParseLength(const std::string& digits, size_t& outLength)
	{
		if (digits.empty())
			return false;
		size_t length = 0;
		for (char c : digits)
		{
			if (c < '0' || c > '9')
				return false;
			length = length * 10 + static_cast<size_t>(c - '0');
			if (length > kMaxContentLength)
				return false;
		}
		outLength = length;
		return true;
	}

	std::string Reader::Trim(const std::string& value)
	{
		const size_t start = value.find_first_not_of(" \t");
		if (start == npos)
			return "";
		const size_t end = value.find_last_not_of(" \t");
		return value.substr(start, end - start + 1);
	}
} // namespace Json
