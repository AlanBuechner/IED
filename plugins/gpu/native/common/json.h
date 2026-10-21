// json.h - JSON written as text (JsonWriter, with commas and nesting kept for the caller) and read into a small tree
// (JsonValue): the replay protocol's messages, the capture's metadata and the API Calls window's descriptions.
#pragma once

#include "fb_base.h"
#include <map>

namespace fb
{
	class JsonWriter
	{
	public:
		JsonWriter& BeginObject(const char* key = nullptr);
		JsonWriter& EndObject();
		JsonWriter& BeginArray(const char* key = nullptr);
		JsonWriter& EndArray();
		JsonWriter& Key(const char* key);

		JsonWriter& Null(const char* key = nullptr);
		JsonWriter& Bool(const char* key, bool value);
		JsonWriter& Int(const char* key, i64 value);
		JsonWriter& Uint(const char* key, u64 value);
		// NaN and the infinities are written as the strings "NaN", "Inf" and "-Inf" (JSON has no such numbers).
		JsonWriter& Double(const char* key, double value);
		JsonWriter& String(const char* key, const std::string& value);
		JsonWriter& String(const char* key, const char* value);
		// `json` is already JSON text.
		JsonWriter& Raw(const char* key, const std::string& json);

		// Values inside an array.
		JsonWriter& Int(i64 value) { return Int(nullptr, value); }
		JsonWriter& Uint(u64 value) { return Uint(nullptr, value); }
		JsonWriter& Double(double value) { return Double(nullptr, value); }
		JsonWriter& String(const std::string& value) { return String(nullptr, value); }
		JsonWriter& Bool(bool value) { return Bool(nullptr, value); }

		const std::string& Text() const { return text_; }
		std::string Take() { return std::move(text_); }
		bool Empty() const { return text_.empty(); }

		static void Escape(std::string& out, const char* text, size_t length);

	private:
		void Separate(const char* key);

		std::string text_;
		std::vector<bool> first_;   // per open container: no member written yet
	};

	enum class JsonKind { Null, Bool, Number, String, Array, Object };

	class JsonValue
	{
	public:
		JsonKind kind = JsonKind::Null;
		bool boolean = false;
		double number = 0;
		std::string text;
		std::vector<JsonValue> items;
		std::vector<std::pair<std::string, JsonValue>> members;

		// The member `key`, or a shared null value.
		const JsonValue& operator[](const char* key) const;
		const JsonValue& At(size_t index) const;
		bool Has(const char* key) const;
		bool IsNull() const { return kind == JsonKind::Null; }

		i64 Int(i64 fallback = 0) const { return kind == JsonKind::Number ? (i64)number : fallback; }
		u64 Uint(u64 fallback = 0) const { return kind == JsonKind::Number ? (u64)number : fallback; }
		double Number(double fallback = 0) const { return kind == JsonKind::Number ? number : fallback; }
		bool Bool(bool fallback = false) const { return kind == JsonKind::Bool ? boolean : fallback; }
		std::string Str(const char* fallback = "") const { return kind == JsonKind::String ? text : std::string(fallback); }
		size_t Count() const { return kind == JsonKind::Array ? items.size() : members.size(); }

		// The value's JSON text.
		std::string Dump() const;
		void Dump(JsonWriter& json, const char* key) const;

		// Parses `text`; false with `error` (and the offset in it) when it is not JSON.
		static bool Parse(const char* text, size_t length, JsonValue& out, std::string& error);
	};
}
