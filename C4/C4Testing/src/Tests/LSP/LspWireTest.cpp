#include "LspWireTest.h"
#include "Protocol/PositionCodec.h"
#include "LspTestSupport.h"
#include "Server/LspServer.h"
#include <memory>
#include <vector>

using namespace C4LSP::Protocol;
using Json::Value;

namespace Testing
{
	// "\xc3\xa9" is e-acute (2 bytes, 1 unit); "\xf0\x9f\x98\x80" an emoji (4 bytes, 2 units).
	static const std::string kLine = "a\xc3\xa9" "b\xf0\x9f\x98\x80" "c";

	void LspWireTest::TestLengthsCountUtf16UnitsAndUtf8Bytes()
	{
		TEST_ASSERT_EQ(Utf16Length(kLine, 0), 0u);
		TEST_ASSERT_EQ(Utf16Length(kLine, 3), 2u);  // a, e-acute
		TEST_ASSERT_EQ(Utf16Length(kLine, 8), 5u);  // ... b, the emoji's two units
		TEST_ASSERT_EQ(Utf16Length(kLine, 9), 6u);
		TEST_ASSERT_EQ(Utf16Length(kLine, 12), 9u); // past the end: one unit a byte
		TEST_ASSERT_EQ(Utf8Length(kLine, 2), 3u);
		TEST_ASSERT_EQ(Utf8Length(kLine, 5), 8u);
		TEST_ASSERT_EQ(Utf8Length(kLine, 6), 9u);
		TEST_ASSERT_EQ(Utf8Length(kLine, 8), 11u);
		TEST_ASSERT_EQ(Utf16Length("\xff" "x", 2), 2u); // a byte that starts no sequence counts as one
		TEST_PASSED();
	}

	static Value Strings(std::initializer_list<const char*> items)
	{
		Value list = Value::MakeArray();
		for (const char* item : items)
			list.Append(Value::String(Core::String(item)));
		return list;
	}

	static Value OfferedEncodings(Value offered)
	{
		Value general = Value::MakeObject();
		general.SetMember("positionEncodings", offered);
		Value capabilities = Value::MakeObject();
		capabilities.SetMember("general", general);
		Value params = Value::MakeObject();
		params.SetMember("capabilities", capabilities);
		return params;
	}

	void LspWireTest::TestNegotiationPrefersUtf8WhenOffered()
	{
		TEST_ASSERT(NegotiatePositionEncoding(OfferedEncodings(Strings({ "utf-16", "utf-8" }))) == PositionEncoding::Utf8);
		TEST_ASSERT(NegotiatePositionEncoding(OfferedEncodings(Strings({ "utf-16" }))) == PositionEncoding::Utf16);
		TEST_ASSERT(NegotiatePositionEncoding(Value::MakeObject()) == PositionEncoding::Utf16); // offers nothing: the default
		TEST_ASSERT_EQ(std::string(PositionEncodingName(PositionEncoding::Utf8)), std::string("utf-8"));
		TEST_PASSED();
	}

	static Value Position(int64 line, int64 character)
	{
		Value position = Value::MakeObject();
		position.SetMember("line", Value::Int(line));
		position.SetMember("character", Value::Int(character));
		return position;
	}

	static Value Range(int64 line, int64 start, int64 end)
	{
		Value range = Value::MakeObject();
		range.SetMember("start", Position(line, start));
		range.SetMember("end", Position(line, end));
		return range;
	}

	void LspWireTest::TestMessagesAreRewrittenByTheNearestUri()
	{
		PositionCodec codec([](const std::string& uri) { return uri == "file:///a.cf" ? std::string("x\n") + kLine : std::string("plain\n" "plain"); });

		// A hover: its range is the request's document's (the default), line 1 being kLine.
		Value hover = Value::MakeObject();
		hover.SetMember("range", Range(1, 3, 9));
		Value converted = codec.ToClient(hover, "file:///a.cf");
		TEST_ASSERT_EQ(converted.Member("range").Member("start").Member("character").GetInt(), int64(2));
		TEST_ASSERT_EQ(converted.Member("range").Member("end").Member("character").GetInt(), int64(6));
		TEST_ASSERT_EQ(codec.FromClient(converted, "file:///a.cf").Member("range").Member("end").Member("character").GetInt(), int64(9));

		// Locations name their own document; another file's ASCII line keeps its columns.
		Value locations = Value::MakeArray();
		Value here = Value::MakeObject();
		here.SetMember("uri", Value::String(Core::String("file:///a.cf")));
		here.SetMember("range", Range(1, 8, 8));
		Value there = Value::MakeObject();
		there.SetMember("uri", Value::String(Core::String("file:///b.cf")));
		there.SetMember("range", Range(1, 8, 8));
		locations.Append(here);
		locations.Append(there);
		Value both = codec.ToClient(locations, "file:///other.cf");
		TEST_ASSERT_EQ(both.At(0).Member("range").Member("start").Member("character").GetInt(), int64(5));
		TEST_ASSERT_EQ(both.At(1).Member("range").Member("start").Member("character").GetInt(), int64(8));

		// A WorkspaceEdit's `changes` names each document by its key.
		Value edit = Value::MakeObject();
		Value textEdit = Value::MakeObject();
		textEdit.SetMember("range", Range(1, 3, 3));
		textEdit.SetMember("newText", Value::String(Core::String("y")));
		Value edits = Value::MakeArray();
		edits.Append(textEdit);
		Value changes = Value::MakeObject();
		changes.SetMember("file:///a.cf", edits);
		edit.SetMember("changes", changes);
		Value moved = codec.ToClient(edit, "file:///other.cf");
		TEST_ASSERT_EQ(moved.Member("changes").Member("file:///a.cf").At(0).Member("range").Member("start").Member("character").GetInt(), int64(2));
		TEST_ASSERT_EQ(std::string(moved.Member("changes").Member("file:///a.cf").At(0).Member("newText").GetString()), std::string("y"));
		TEST_PASSED();
	}

	// A server fed messages in-process; what it sends back, parsed.
	class WireServer
	{
	public:
		WireServer() : m_Server(std::make_unique<C4LSP::Server::LspServer>([this](const std::string& framed) { m_Framed.push_back(framed); })) {}

		void Feed(const std::string& method, const Value& id, const Value& params)
		{
			Value message = Value::MakeObject();
			message.SetMember("jsonrpc", Value::String(Core::String("2.0")));
			if (!id.IsNull())
				message.SetMember("id", id);
			message.SetMember("method", Value::String(Core::String(method)));
			message.SetMember("params", params);
			m_Server->HandleMessage(message);
		}

		// The answer whose id serializes as `id` does; null when there is none.
		Value Answer(const Value& id) const
		{
			for (const std::string& framed : m_Framed)
			{
				Value message(Core::LoadObjectDescriptionFromJsonString(framed.substr(framed.find("\r\n\r\n") + 4)));
				if (message.HasMember("id") && message.Member("id").Serialize() == id.Serialize())
					return message;
			}
			return Value::Null();
		}

		C4LSP::Server::LspServer& Server() { return *m_Server; }

	private:
		std::vector<std::string> m_Framed;
		std::unique_ptr<C4LSP::Server::LspServer> m_Server;
	};

	static Value HoverAt(const std::string& uri, uint32 line, uint32 character)
	{
		Value document = Value::MakeObject();
		document.SetMember("uri", Value::String(Core::String(uri)));
		Value params = Value::MakeObject();
		params.SetMember("textDocument", document);
		params.SetMember("position", Position(line, character));
		return params;
	}

	// The column a hover on `value`'s use answers in, asked at that use as `encoding` counts it.
	static int64 HoverColumn(Value offered, bool utf8)
	{
		// The e-acute before the use makes its UTF-8 column one more than its UTF-16 one.
		const std::string program = "int32 value = 1;\nint32 Run() { return /* \xc3\xa9 */ value; }\n";
		const std::string uri = Testing::Lsp::UriFor("Wire.cf");
		WireServer wire;
		wire.Feed("initialize", Value::Int(1), OfferedEncodings(offered));
		wire.Feed("initialized", Value::Null(), Value::MakeObject());
		Value document = Value::MakeObject();
		document.SetMember("uri", Value::String(Core::String(uri)));
		document.SetMember("languageId", Value::String(Core::String("c4")));
		document.SetMember("version", Value::Int(1));
		document.SetMember("text", Value::String(Core::String(program)));
		Value opened = Value::MakeObject();
		opened.SetMember("textDocument", document);
		wire.Feed("textDocument/didOpen", Value::Null(), opened);
		wire.Server().WaitForCurrentWork();
		const uint32 bytes = Testing::Lsp::PositionOf(program, "value", 2).character;
		wire.Feed("textDocument/hover", Value::Int(2), HoverAt(uri, 1, utf8 ? bytes : bytes - 1));
		const Value answer = wire.Answer(Value::Int(2));
		return answer.Member("result").Member("range").Member("start").Member("character").GetInt(-1);
	}

	void LspWireTest::TestServerAnswersInTheNegotiatedEncoding()
	{
		WireServer wire;
		wire.Feed("initialize", Value::Int(1), OfferedEncodings(Strings({ "utf-8", "utf-16" })));
		TEST_ASSERT_EQ(std::string(wire.Answer(Value::Int(1)).Member("result").Member("capabilities").Member("positionEncoding").GetString()), std::string("utf-8"));
		WireServer plain;
		plain.Feed("initialize", Value::Int(1), Value::MakeObject());
		TEST_ASSERT_EQ(std::string(plain.Answer(Value::Int(1)).Member("result").Member("capabilities").Member("positionEncoding").GetString()), std::string("utf-16"));

		const int64 bytes = static_cast<int64>(Testing::Lsp::PositionOf("int32 Run() { return /* \xc3\xa9 */ value; }", "value").character);
		TEST_ASSERT_EQ(HoverColumn(Strings({ "utf-8" }), true), bytes);
		TEST_ASSERT_EQ(HoverColumn(Strings({ "utf-16" }), false), bytes - 1);
		TEST_PASSED();
	}

	void LspWireTest::TestStringRequestIdIsEchoed()
	{
		WireServer wire;
		wire.Feed("initialize", Value::String(Core::String("init-1")), Value::MakeObject());
		TEST_ASSERT(wire.Answer(Value::String(Core::String("init-1"))).Member("result").IsObject());
		wire.Feed("no/such/method", Value::String(Core::String("x")), Value::MakeObject());
		TEST_ASSERT_EQ(wire.Answer(Value::String(Core::String("x"))).Member("error").Member("code").GetInt(), int64(-32601));
		TEST_PASSED();
	}
} // namespace Testing
