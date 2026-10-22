// shaderedit.cpp - shader keys and compiler logs (shaderedit.h).
#include "shaderedit.h"

#include <cstdlib>

namespace fb
{
	namespace
	{
		bool Digits(const std::string& text, size_t& at, u32& value) {
			const size_t start = at;
			value = 0;
			while (at < text.size() && text[at] >= '0' && text[at] <= '9') value = value * 10 + (u32)(text[at++] - '0');
			return at > start;
		}

		std::string Trimmed(const std::string& text) {
			size_t a = 0, b = text.size();
			while (a < b && (text[a] == ' ' || text[a] == '\t')) ++a;
			while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r')) --b;
			return text.substr(a, b - a);
		}

		// After the location: "error X3000: text", "error: text", "warning: text".
		bool Message(const std::string& rest, Diagnostic& d) {
			std::string text = Trimmed(rest);
			for (const char* severity : {"error", "warning", "note"}) {
				const size_t n = strlen(severity);
				if (text.compare(0, n, severity) != 0) continue;
				d.severity = severity;
				const size_t colon = text.find(':', n);
				if (colon == std::string::npos) return false;
				d.text = Trimmed(text.substr(colon + 1));
				return true;
			}
			return false;
		}

		bool ReadLine(const std::string& line, Diagnostic& d) {
			// Flashbang's assembler: "line 12: text".
			if (line.compare(0, 5, "line ") == 0) {
				size_t at = 5;
				if (!Digits(line, at, d.line) || at >= line.size() || line[at] != ':') return false;
				d.severity = "error";
				d.text = Trimmed(line.substr(at + 1));
				return true;
			}
			// fxc: "name(12,5-9): error X3000: text".
			for (size_t open = line.find('('); open != std::string::npos; open = line.find('(', open + 1)) {
				size_t at = open + 1;
				if (!Digits(line, at, d.line)) continue;
				if (at < line.size() && line[at] == ',') {
					++at;
					Digits(line, at, d.column);
					while (at < line.size() && line[at] != ')') ++at;
				}
				if (at + 1 < line.size() && line[at] == ')' && line[at + 1] == ':' && Message(line.substr(at + 2), d)) return true;
			}
			// dxc and glslang: "name:12:5: error: text" (a drive letter's colon comes first, and is skipped).
			for (size_t colon = line.find(':'); colon != std::string::npos; colon = line.find(':', colon + 1)) {
				size_t at = colon + 1;
				if (!Digits(line, at, d.line) || at >= line.size() || line[at] != ':') continue;
				++at;
				const size_t afterLine = at;
				if (Digits(line, at, d.column) && at < line.size() && line[at] == ':') ++at;
				else at = afterLine, d.column = 0;
				if (Message(line.substr(at), d)) return true;
			}
			return false;
		}
	}

	std::string ShaderKey(const void* bytes, size_t size) {
		u64 hash = 14695981039346656037ull;
		for (size_t i = 0; i < size; ++i) hash = (hash ^ ((const u8*)bytes)[i]) * 1099511628211ull;
		return Format("%016llx", (unsigned long long)hash);
	}

	void WriteDiagnostics(JsonWriter& out, const std::string& log) {
		out.String("log", log);
		out.BeginArray("diagnostics");
		for (const Diagnostic& d : ParseCompilerLog(log))
			out.BeginObject().Uint("line", d.line).Uint("column", d.column).String("severity", d.severity).String("text", d.text).EndObject();
		out.EndArray();
	}

	std::vector<Diagnostic> ParseCompilerLog(const std::string& log) {
		std::vector<Diagnostic> out;
		size_t at = 0;
		while (at < log.size()) {
			size_t end = log.find('\n', at);
			if (end == std::string::npos) end = log.size();
			Diagnostic d;
			if (ReadLine(log.substr(at, end - at), d)) out.push_back(d);
			at = end + 1;
		}
		return out;
	}
}
