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
		return VectorType(base, info->channels) + " " + name;
	}

	std::string VectorType(const std::string& base, u32 components) {
		if (components <= 1) return base;
		return base + (isdigit((unsigned char)base.back()) ? "x" : "") + std::to_string(components);
	}

	std::string BlockLayout(const std::vector<BlockMember>& members, u32 blockBytes) {
		std::vector<LayoutField> fields;
		for (const BlockMember& m : members) {
			if (!m.elements) continue;    // a runtime array: its elements are the buffer's rows, not the block's
			if (m.vectorType.empty()) {
				if (m.bytes >= 4) fields.push_back({m.offset, m.bytes / 4 * 4, "uint " + m.name + "[" + std::to_string(m.bytes / 4) + "]"});
				continue;
			}
			const bool tightVectors = m.vectors == 1 || m.vectorStride == m.vectorBytes;
			const u32 elementBytes = tightVectors ? m.vectors * m.vectorBytes : (m.vectors - 1) * m.vectorStride + m.vectorBytes;
			const bool tightElements = m.elements == 1 || m.elementStride == elementBytes;
			const std::string type = m.vectors > 1 ? m.vectorType + "x" + std::to_string(m.vectors) : m.vectorType;
			if (tightVectors && tightElements)
				fields.push_back({m.offset, m.elements * elementBytes, type + " " + m.name + (m.elements > 1 ? "[" + std::to_string(m.elements) + "]" : "")});
			else
				for (u32 e = 0; e < m.elements; ++e) {
					const std::string element = m.name + (m.elements > 1 ? "_" + std::to_string(e) : "");
					const u32 at = m.offset + e * m.elementStride;
					if (tightVectors) fields.push_back({at, elementBytes, type + " " + element});
					else
						for (u32 v = 0; v < m.vectors; ++v) fields.push_back({at + v * m.vectorStride, m.vectorBytes, m.vectorType + " " + element + "_" + std::to_string(v)});
				}
			if (fields.size() > 512) return "";
		}
		return BufferLayout(fields, blockBytes);
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
