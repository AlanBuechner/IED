#include "bufferlayout.h"

#include <algorithm>
#include <cctype>

namespace fb
{
	static const char* BaseOf(Component component, u8 bits) {
		switch (component) {
			case Component::Float: return bits == 16 ? "half" : bits == 64 ? "double" : bits == 32 ? "float" : nullptr;
			case Component::UNorm:
			case Component::Srgb: return bits == 8 ? "unorm8" : bits == 16 ? "unorm16" : nullptr;
			case Component::SNorm: return bits == 8 ? "snorm8" : bits == 16 ? "snorm16" : nullptr;
			case Component::SInt: return bits == 8 ? "int8" : bits == 16 ? "int16" : bits == 32 ? "int" : nullptr;
			case Component::UInt:
			case Component::Typeless: return bits == 8 ? "uint8" : bits == 16 ? "uint16" : bits == 32 ? "uint" : nullptr;
			default: return nullptr;
		}
	}

	std::string FieldOf(const FormatInfo* info, const std::string& name) {
		if (!info || !info->bytes) return "";
		const char* base = info->codec == Codec::Channels ? BaseOf(info->component, info->bits) : nullptr;
		if (!base) return info->bytes == 4 ? "uint " + name : "uint8 " + name + "[" + std::to_string(info->bytes) + "]";
		std::string type = base;
		if (info->channels > 1) type += (isdigit((unsigned char)type.back()) ? "x" : "") + std::to_string(info->channels);
		return type + " " + name;
	}

	std::string BufferLayout(std::vector<LayoutField> fields, u32 stride) {
		std::stable_sort(fields.begin(), fields.end(), [](const LayoutField& a, const LayoutField& b) { return a.offset < b.offset; });
		std::string layout;
		u32 at = 0;
		const auto add = [&](const std::string& text) { layout += (layout.empty() ? "" : "; ") + text; };
		for (const LayoutField& field : fields) {
			if (field.text.empty() || field.offset < at) continue;
			if (field.offset > at) add("pad " + std::to_string(field.offset - at));
			add(field.text);
			at = field.offset + field.bytes;
		}
		if (!layout.empty() && stride > at) add("pad " + std::to_string(stride - at));
		return layout;
	}
}
