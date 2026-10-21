#pragma once
#include "Json/JsonValue.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace C4LSP::Protocol
{
	/// The position encoding a client and the server agreed on (LSP 3.17 `positionEncoding`). The compiler counts a
	/// column in UTF-8 bytes, so with Utf8 positions pass as they are.
	enum class PositionEncoding { Utf8, Utf16 };

	/// The encoding `initialize`'s params ask for: utf-8 when the client offers it in
	/// `capabilities.general.positionEncodings`, else UTF-16 (the protocol's default).
	PositionEncoding NegotiatePositionEncoding(const Json::Value& initializeParams);
	const char* PositionEncodingName(PositionEncoding encoding);

	/// The UTF-16 length of `text`'s first `utf8Length` bytes (a broken sequence counts byte by byte), and the number of
	/// bytes that hold its first `utf16Length` UTF-16 units (a column past the end stays past it).
	uint32 Utf16Length(const std::string& text, uint32 utf8Length);
	uint32 Utf8Length(const std::string& text, uint32 utf16Length);

	/// Rewrites every position of a message between the server's UTF-8 columns and a client's UTF-16 ones. A position is
	/// any object with numeric `line` and `character`; it belongs to the document the nearest `uri` / `targetUri` /
	/// `textDocument.uri` at or above it names (or the key of a WorkspaceEdit's `changes`), else to `defaultUri`. A
	/// document's text comes from `textOf` once per codec, so make one per message.
	class PositionCodec
	{
	public:
		using TextOf = std::function<std::string(const std::string& uri)>;
		explicit PositionCodec(TextOf textOf) : m_TextOf(std::move(textOf)) {}

		Json::Value ToClient(const Json::Value& message, const std::string& defaultUri) { return Rewrite(message, defaultUri, true); }
		Json::Value FromClient(const Json::Value& message, const std::string& defaultUri) { return Rewrite(message, defaultUri, false); }

		uint32 ToClientColumn(const std::string& uri, uint32 line, uint32 utf8Column) { return Utf16Length(LineOf(uri, line), utf8Column); }
		uint32 FromClientColumn(const std::string& uri, uint32 line, uint32 utf16Column) { return Utf8Length(LineOf(uri, line), utf16Column); }

	private:
		Json::Value Rewrite(const Json::Value& node, const std::string& uri, bool toClient);
		const std::string& LineOf(const std::string& uri, uint32 line);

		TextOf m_TextOf;
		std::unordered_map<std::string, std::vector<std::string>> m_Lines;
	};
} // namespace C4LSP::Protocol
