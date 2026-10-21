#include "json.h"

#include <cmath>
#include <cstdlib>

namespace fb
{
	void JsonWriter::Escape(std::string& out, const char* text, size_t length) {
		out += '"';
		for (size_t i = 0; i < length; ++i) {
			const unsigned char c = (unsigned char)text[i];
			switch (c) {
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (c < 0x20) {
						char escaped[8];
						snprintf(escaped, sizeof(escaped), "\\u%04x", c);
						out += escaped;
					}
					else out += (char)c;
			}
		}
		out += '"';
	}

	void JsonWriter::Separate(const char* key) {
		if (!first_.empty()) {
			if (!first_.back()) text_ += ',';
			first_.back() = false;
		}
		if (key) {
			Escape(text_, key, strlen(key));
			text_ += ':';
		}
	}

	JsonWriter& JsonWriter::BeginObject(const char* key) {
		Separate(key);
		text_ += '{';
		first_.push_back(true);
		return *this;
	}

	JsonWriter& JsonWriter::EndObject() {
		text_ += '}';
		first_.pop_back();
		return *this;
	}

	JsonWriter& JsonWriter::BeginArray(const char* key) {
		Separate(key);
		text_ += '[';
		first_.push_back(true);
		return *this;
	}

	JsonWriter& JsonWriter::EndArray() {
		text_ += ']';
		first_.pop_back();
		return *this;
	}

	JsonWriter& JsonWriter::Key(const char* key) {
		Separate(key);
		// The value that follows must not add a separator of its own.
		if (!first_.empty()) first_.back() = true;
		return *this;
	}

	JsonWriter& JsonWriter::Null(const char* key) {
		Separate(key);
		text_ += "null";
		return *this;
	}

	JsonWriter& JsonWriter::Bool(const char* key, bool value) {
		Separate(key);
		text_ += value ? "true" : "false";
		return *this;
	}

	JsonWriter& JsonWriter::Int(const char* key, i64 value) {
		Separate(key);
		text_ += std::to_string(value);
		return *this;
	}

	JsonWriter& JsonWriter::Uint(const char* key, u64 value) {
		Separate(key);
		text_ += std::to_string(value);
		return *this;
	}

	JsonWriter& JsonWriter::Double(const char* key, double value) {
		Separate(key);
		if (std::isnan(value)) text_ += "\"NaN\"";
		else if (std::isinf(value)) text_ += value > 0 ? "\"Inf\"" : "\"-Inf\"";
		else {
			char number[40];
			snprintf(number, sizeof(number), "%.9g", value);
			text_ += number;
		}
		return *this;
	}

	JsonWriter& JsonWriter::String(const char* key, const std::string& value) {
		Separate(key);
		Escape(text_, value.data(), value.size());
		return *this;
	}

	JsonWriter& JsonWriter::String(const char* key, const char* value) {
		Separate(key);
		Escape(text_, value ? value : "", value ? strlen(value) : 0);
		return *this;
	}

	JsonWriter& JsonWriter::Raw(const char* key, const std::string& json) {
		Separate(key);
		text_ += json.empty() ? std::string("null") : json;
		return *this;
	}

	static const JsonValue& NullValue() {
		static const JsonValue none;
		return none;
	}

	const JsonValue& JsonValue::operator[](const char* key) const {
		if (kind != JsonKind::Object) return NullValue();
		for (const auto& member : members)
			if (member.first == key) return member.second;
		return NullValue();
	}

	const JsonValue& JsonValue::At(size_t index) const {
		if (kind != JsonKind::Array || index >= items.size()) return NullValue();
		return items[index];
	}

	bool JsonValue::Has(const char* key) const {
		if (kind != JsonKind::Object) return false;
		for (const auto& member : members)
			if (member.first == key) return true;
		return false;
	}

	void JsonValue::Dump(JsonWriter& json, const char* key) const {
		switch (kind) {
			case JsonKind::Null: json.Null(key); break;
			case JsonKind::Bool: json.Bool(key, boolean); break;
			case JsonKind::Number:
				if (number == std::floor(number) && std::fabs(number) < 9.0e15) json.Int(key, (i64)number);
				else json.Double(key, number);
				break;
			case JsonKind::String: json.String(key, text); break;
			case JsonKind::Array:
				json.BeginArray(key);
				for (const JsonValue& item : items) item.Dump(json, nullptr);
				json.EndArray();
				break;
			case JsonKind::Object:
				json.BeginObject(key);
				for (const auto& member : members) member.second.Dump(json, member.first.c_str());
				json.EndObject();
				break;
		}
	}

	std::string JsonValue::Dump() const {
		JsonWriter json;
		Dump(json, nullptr);
		return json.Take();
	}

	namespace
	{
		class Parser
		{
		public:
			Parser(const char* text, size_t length) : at_(text), begin_(text), end_(text + length) {}

			bool Document(JsonValue& out, std::string& error) {
				if (!Value(out, 0)) {
					error = Format("%s at offset %zu", problem_.c_str(), (size_t)(at_ - begin_));
					return false;
				}
				Space();
				if (at_ != end_) {
					error = Format("unexpected text after the value at offset %zu", (size_t)(at_ - begin_));
					return false;
				}
				return true;
			}

		private:
			void Space() {
				while (at_ < end_ && (*at_ == ' ' || *at_ == '\t' || *at_ == '\n' || *at_ == '\r')) ++at_;
			}

			bool Fail(const char* why) {
				problem_ = why;
				return false;
			}

			bool Literal(const char* word) {
				const size_t length = strlen(word);
				if ((size_t)(end_ - at_) < length || memcmp(at_, word, length) != 0) return Fail("bad literal");
				at_ += length;
				return true;
			}

			static void AppendUtf8(std::string& out, u32 code) {
				if (code < 0x80) out += (char)code;
				else if (code < 0x800) {
					out += (char)(0xC0 | (code >> 6));
					out += (char)(0x80 | (code & 0x3F));
				}
				else if (code < 0x10000) {
					out += (char)(0xE0 | (code >> 12));
					out += (char)(0x80 | ((code >> 6) & 0x3F));
					out += (char)(0x80 | (code & 0x3F));
				}
				else {
					out += (char)(0xF0 | (code >> 18));
					out += (char)(0x80 | ((code >> 12) & 0x3F));
					out += (char)(0x80 | ((code >> 6) & 0x3F));
					out += (char)(0x80 | (code & 0x3F));
				}
			}

			bool Hex4(u32& code) {
				if (end_ - at_ < 4) return Fail("short \\u escape");
				code = 0;
				for (int i = 0; i < 4; ++i) {
					const char c = *at_++;
					code <<= 4;
					if (c >= '0' && c <= '9') code |= (u32)(c - '0');
					else if (c >= 'a' && c <= 'f') code |= (u32)(c - 'a' + 10);
					else if (c >= 'A' && c <= 'F') code |= (u32)(c - 'A' + 10);
					else return Fail("bad \\u escape");
				}
				return true;
			}

			bool Text(std::string& out) {
				++at_;
				while (at_ < end_ && *at_ != '"') {
					char c = *at_++;
					if (c != '\\') {
						out += c;
						continue;
					}
					if (at_ >= end_) return Fail("unterminated string");
					c = *at_++;
					switch (c) {
						case '"': out += '"'; break;
						case '\\': out += '\\'; break;
						case '/': out += '/'; break;
						case 'b': out += '\b'; break;
						case 'f': out += '\f'; break;
						case 'n': out += '\n'; break;
						case 'r': out += '\r'; break;
						case 't': out += '\t'; break;
						case 'u': {
							u32 code;
							if (!Hex4(code)) return false;
							if (code >= 0xD800 && code < 0xDC00 && end_ - at_ >= 6 && at_[0] == '\\' && at_[1] == 'u') {
								at_ += 2;
								u32 low;
								if (!Hex4(low)) return false;
								code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
							}
							AppendUtf8(out, code);
							break;
						}
						default: return Fail("bad escape");
					}
				}
				if (at_ >= end_) return Fail("unterminated string");
				++at_;
				return true;
			}

			bool Value(JsonValue& out, int depth) {
				if (depth > 256) return Fail("nested too deeply");
				Space();
				if (at_ >= end_) return Fail("unexpected end");
				const char c = *at_;
				if (c == '{') {
					out.kind = JsonKind::Object;
					++at_;
					Space();
					if (at_ < end_ && *at_ == '}') {
						++at_;
						return true;
					}
					for (;;) {
						Space();
						if (at_ >= end_ || *at_ != '"') return Fail("expected a key");
						std::string key;
						if (!Text(key)) return false;
						Space();
						if (at_ >= end_ || *at_ != ':') return Fail("expected ':'");
						++at_;
						out.members.emplace_back(std::move(key), JsonValue());
						if (!Value(out.members.back().second, depth + 1)) return false;
						Space();
						if (at_ < end_ && *at_ == ',') {
							++at_;
							continue;
						}
						if (at_ < end_ && *at_ == '}') {
							++at_;
							return true;
						}
						return Fail("expected ',' or '}'");
					}
				}
				if (c == '[') {
					out.kind = JsonKind::Array;
					++at_;
					Space();
					if (at_ < end_ && *at_ == ']') {
						++at_;
						return true;
					}
					for (;;) {
						out.items.emplace_back();
						if (!Value(out.items.back(), depth + 1)) return false;
						Space();
						if (at_ < end_ && *at_ == ',') {
							++at_;
							continue;
						}
						if (at_ < end_ && *at_ == ']') {
							++at_;
							return true;
						}
						return Fail("expected ',' or ']'");
					}
				}
				if (c == '"') {
					out.kind = JsonKind::String;
					return Text(out.text);
				}
				if (c == 't') {
					out.kind = JsonKind::Bool;
					out.boolean = true;
					return Literal("true");
				}
				if (c == 'f') {
					out.kind = JsonKind::Bool;
					return Literal("false");
				}
				if (c == 'n') {
					out.kind = JsonKind::Null;
					return Literal("null");
				}
				if (c == '-' || (c >= '0' && c <= '9')) {
					char* stop = nullptr;
					std::string number(at_, (size_t)std::min<ptrdiff_t>(end_ - at_, 64));
					out.number = strtod(number.c_str(), &stop);
					if (stop == number.c_str()) return Fail("bad number");
					at_ += stop - number.c_str();
					out.kind = JsonKind::Number;
					return true;
				}
				return Fail("unexpected character");
			}

			const char* at_;
			const char* begin_;
			const char* end_;
			std::string problem_;
		};
	}

	bool JsonValue::Parse(const char* text, size_t length, JsonValue& out, std::string& error) {
		out = JsonValue();
		Parser parser(text, length);
		return parser.Document(out, error);
	}
}
