#include "Protocol/PositionCodec.h"
#include <algorithm>

namespace C4LSP::Protocol
{
	PositionEncoding NegotiatePositionEncoding(const Json::Value& initializeParams)
	{
		const Json::Value offered = initializeParams.Member("capabilities").Member("general").Member("positionEncodings");
		for (uint32 i = 0; i < offered.Size(); ++i)
			if (offered.At(i).IsString() && offered.At(i).GetString() == "utf-8")
				return PositionEncoding::Utf8;
		return PositionEncoding::Utf16;
	}

	const char* PositionEncodingName(PositionEncoding encoding)
	{
		return encoding == PositionEncoding::Utf8 ? "utf-8" : "utf-16";
	}

	// The bytes of the UTF-8 sequence that starts with `lead`, and the UTF-16 units it makes; one of each for a byte
	// that cannot start a sequence.
	static void SequenceOf(unsigned char lead, uint32& bytes, uint32& units)
	{
		bytes = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
		units = bytes == 4 ? 2 : 1;
	}

	uint32 Utf16Length(const std::string& text, uint32 utf8Length)
	{
		uint32 units = 0;
		uint32 at = 0;
		while (at < utf8Length && at < text.size())
		{
			uint32 bytes, made;
			SequenceOf(static_cast<unsigned char>(text[at]), bytes, made);
			at += bytes;
			units += made;
		}
		return units + (utf8Length > at ? utf8Length - at : 0);
	}

	uint32 Utf8Length(const std::string& text, uint32 utf16Length)
	{
		uint32 units = 0;
		uint32 at = 0;
		while (units < utf16Length && at < text.size())
		{
			uint32 bytes, made;
			SequenceOf(static_cast<unsigned char>(text[at]), bytes, made);
			at += bytes;
			units += made;
		}
		return std::min<uint32>(at, static_cast<uint32>(text.size())) + (utf16Length > units ? utf16Length - units : 0);
	}

	const std::string& PositionCodec::LineOf(const std::string& uri, uint32 line)
	{
		auto found = m_Lines.find(uri);
		if (found == m_Lines.end())
		{
			std::vector<std::string> lines;
			const std::string text = m_TextOf(uri);
			size_t start = 0;
			while (true)
			{
				size_t end = text.find('\n', start);
				lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
				if (end == std::string::npos) break;
				start = end + 1;
			}
			found = m_Lines.emplace(uri, std::move(lines)).first;
		}
		static const std::string kNone;
		return line < found->second.size() ? found->second[line] : kNone;
	}

	static bool IsPosition(const Json::Value& node)
	{
		return node.IsObject() && node.Member("line").IsInt() && node.Member("character").IsInt();
	}

	// The URI an object names for the positions in it.
	static std::string UriOf(const Json::Value& node, const std::string& outer)
	{
		for (const char* key : { "uri", "targetUri" })
			if (node.Member(key).IsString())
				return node.Member(key).GetString();
		if (node.Member("textDocument").Member("uri").IsString())
			return node.Member("textDocument").Member("uri").GetString();
		// An incoming call's `fromRanges` are in the caller's document, which its `from` item names.
		if (node.Member("fromRanges").IsArray() && node.Member("from").Member("uri").IsString())
			return node.Member("from").Member("uri").GetString();
		return outer;
	}

	Json::Value PositionCodec::Rewrite(const Json::Value& node, const std::string& uri, bool toClient)
	{
		if (node.IsArray())
		{
			Json::Value out = Json::Value::MakeArray();
			for (uint32 i = 0; i < node.Size(); ++i)
				out.Append(Rewrite(node.At(i), uri, toClient));
			return out;
		}
		if (!node.IsObject())
			return node;
		if (IsPosition(node))
		{
			const uint32 line = static_cast<uint32>(node.Member("line").GetInt());
			const uint32 character = static_cast<uint32>(node.Member("character").GetInt());
			Json::Value out = node;
			out.SetMember("character", Json::Value::Int(toClient ? ToClientColumn(uri, line, character) : FromClientColumn(uri, line, character)));
			return out;
		}
		const std::string inner = UriOf(node, uri);
		Json::Value out = Json::Value::MakeObject();
		for (const auto& [name, value] : node.Raw().GetAsDescriptionMap())
		{
			const Json::Value member(value);
			// A WorkspaceEdit's `changes` names each document by a key: the edits under it are that document's.
			if (name == "changes" && member.IsObject())
			{
				Json::Value byDocument = Json::Value::MakeObject();
				for (const auto& [document, edits] : member.Raw().GetAsDescriptionMap())
					byDocument.SetMember(document, Rewrite(Json::Value(edits), std::string(document), toClient));
				out.SetMember(name, byDocument);
			}
			else
				out.SetMember(name, Rewrite(member, inner, toClient));
		}
		return out;
	}
} // namespace C4LSP::Protocol
