#include "Target/Values.h"
#include "Target/ScalarText.h"
#include <algorithm>
#include <cstring>

namespace C4DAP::Target
{
	namespace
	{
		constexpr uint64_t kPointerBytes = 8;
		constexpr uint64_t kMaxValueBytes = 64;   // the largest scalar read in one piece
		constexpr size_t kMaxTextBytes = 256;     // a char8* shows at most this much of its text
		constexpr size_t kSummaryMembers = 6;     // an object's one-line summary lists this many fields
		constexpr uint64_t kSummaryElements = 8;  // an array's summary lists this many elements
		constexpr uint64_t kMaxChildren = 1000;   // an array shows at most this many elements
		constexpr int kSummaryDepth = 1;          // nested objects in a summary are {...}

		bool IsText(const TypeFacts& target) { return target.shape == TypeShape::Basic && target.basic == BasicKind::Char8; }
		bool IsObject(TypeShape shape) { return shape == TypeShape::Struct || shape == TypeShape::Union; }
	}

	bool Values::Bytes(const ValueRef& value, uint64_t size, std::vector<uint8_t>& bytes) const
	{
		bytes.assign(static_cast<size_t>(std::min(size, kMaxValueBytes)), 0);
		if (value.inRegister)
		{
			std::memcpy(bytes.data(), &value.registerBits, std::min<size_t>(bytes.size(), sizeof(value.registerBits)));
			return true;
		}
		return m_Process.Read(value.address, bytes.data(), bytes.size());
	}

	std::optional<uint64_t> Values::PointerValue(const ValueRef& pointer) const
	{
		std::vector<uint8_t> bytes;
		if (!Bytes(pointer, kPointerBytes, bytes)) return std::nullopt;
		uint64_t address = 0;
		std::memcpy(&address, bytes.data(), sizeof(address));
		return address;
	}

	std::optional<ValueRef> Values::Target(const ValueRef& pointer) const
	{
		const TypeFacts facts = m_Symbols.Describe(pointer.type);
		if (facts.shape != TypeShape::Pointer && facts.shape != TypeShape::Reference) return std::nullopt;
		if (!facts.element.Valid() || m_Symbols.Describe(facts.element).shape == TypeShape::Void) return std::nullopt;
		const std::optional<uint64_t> address = PointerValue(pointer);
		if (!address || *address == 0) return std::nullopt;
		return ValueRef{ facts.element, *address };
	}

	std::optional<ValueRef> Values::Dereferenced(const ValueRef& value) const
	{
		const TypeShape shape = m_Symbols.Describe(value.type).shape;
		if (shape == TypeShape::Pointer || shape == TypeShape::Reference) return Target(value);
		return value;
	}

	std::string Values::Text(const ValueRef& value) const
	{
		return Summary(value, 0);
	}

	std::string Values::Summary(const ValueRef& value, int depth) const
	{
		const TypeFacts facts = m_Symbols.Describe(value.type);
		std::vector<uint8_t> bytes;
		switch (facts.shape)
		{
		case TypeShape::Basic:
			if (!Bytes(value, facts.size, bytes)) return "<unreadable>";
			return ScalarText(facts.basic, facts.size, bytes.data());
		case TypeShape::Enum: return EnumText(value, facts);
		case TypeShape::Pointer: return PointerText(value, facts);
		case TypeShape::Reference:
		{
			const std::optional<ValueRef> target = Target(value);
			return target ? Summary(*target, depth) : "null";
		}
		case TypeShape::Struct: case TypeShape::Union:
			return depth > kSummaryDepth ? "{...}" : ObjectSummary(value, depth);
		case TypeShape::Array:
			return depth > kSummaryDepth ? "{...}" : ArraySummary(value, facts, depth);
		default: return "";
		}
	}

	std::string Values::EnumText(const ValueRef& value, const TypeFacts& facts) const
	{
		const TypeFacts underlying = m_Symbols.Describe(facts.element);
		std::vector<uint8_t> bytes;
		if (!Bytes(value, facts.size, bytes)) return "<unreadable>";
		const int64_t number = IntegerValue(underlying.shape == TypeShape::Basic ? underlying.basic : BasicKind::Signed, facts.size, bytes.data());
		for (const Enumerator& enumerator : m_Symbols.Enumerators(value.type))
			if (enumerator.value == number) return enumerator.name;
		return std::to_string(number);
	}

	std::string Values::PointerText(const ValueRef& value, const TypeFacts& facts) const
	{
		const std::optional<uint64_t> address = PointerValue(value);
		if (!address) return "<unreadable>";
		if (*address == 0) return "null";
		const TypeFacts target = m_Symbols.Describe(facts.element);
		std::string text = AddressText(*address);
		if (IsText(target))
		{
			bool complete = false;
			text += " " + QuotedText(m_Process.ReadText(*address, kMaxTextBytes, complete), complete);
		}
		else if (target.shape == TypeShape::Function)
		{
			if (const std::optional<FunctionFacts> function = m_Symbols.FunctionOf(CodePlace{ *address })) text += " " + function->name;
		}
		return text;
	}

	std::string Values::ObjectSummary(const ValueRef& value, int depth) const
	{
		std::string text = "{";
		size_t shown = 0;
		for (const MemberFacts& member : m_Symbols.Members(value.type))
		{
			if (member.isBase) continue;
			if (shown == kSummaryMembers) { text += ", ..."; break; }
			if (shown++ > 0) text += ", ";
			text += member.name + "=" + Summary(ValueRef{ member.type, value.address + member.offset }, depth + 1);
		}
		return text + "}";
	}

	std::string Values::ArraySummary(const ValueRef& value, const TypeFacts& facts, int depth) const
	{
		const uint64_t elementSize = facts.count ? facts.size / facts.count : 0;
		std::string text = "{";
		for (uint64_t i = 0; i < facts.count; ++i)
		{
			if (i == kSummaryElements) { text += ", ..."; break; }
			if (i > 0) text += ", ";
			text += Summary(ValueRef{ facts.element, value.address + i * elementSize }, depth + 1);
		}
		return text + "}";
	}

	std::string Values::TypeName(TypeRef type) const
	{
		const TypeFacts facts = m_Symbols.Describe(type);
		if (!facts.name.empty()) return facts.name;
		switch (facts.shape)
		{
		case TypeShape::Void: return "void";
		case TypeShape::Basic: return BasicTypeName(facts.basic, facts.size);
		case TypeShape::Pointer:
		{
			if (!facts.element.Valid()) return "void*";
			if (m_Symbols.Describe(facts.element).shape == TypeShape::Function) return "funcptr";
			return TypeName(facts.element) + "*";
		}
		case TypeShape::Reference: return TypeName(facts.element) + "&";
		case TypeShape::Array: return TypeName(facts.element) + "[" + std::to_string(facts.count) + "]";
		case TypeShape::Function: return "function";
		default: return "?";
		}
	}

	bool Values::HasChildren(const ValueRef& value) const
	{
		const TypeFacts facts = m_Symbols.Describe(value.type);
		switch (facts.shape)
		{
		case TypeShape::Struct: case TypeShape::Union: return !value.inRegister && !m_Symbols.Members(value.type).empty();
		case TypeShape::Array: return !value.inRegister && facts.count > 0;
		case TypeShape::Pointer:
		{
			const std::optional<ValueRef> target = Target(value);
			return target && !IsText(m_Symbols.Describe(target->type)) && m_Symbols.Describe(target->type).shape != TypeShape::Function;
		}
		case TypeShape::Reference:
		{
			const std::optional<ValueRef> target = Target(value);
			return target && HasChildren(*target);
		}
		default: return false;
		}
	}

	std::vector<NamedValue> Values::Children(const ValueRef& value) const
	{
		std::vector<NamedValue> children;
		const TypeFacts facts = m_Symbols.Describe(value.type);
		switch (facts.shape)
		{
		case TypeShape::Struct: case TypeShape::Union:
			for (const MemberFacts& member : m_Symbols.Members(value.type))
				children.push_back(NamedValue{ member.name, ValueRef{ member.type, value.address + member.offset } });
			break;
		case TypeShape::Array:
		{
			const uint64_t elementSize = facts.count ? facts.size / facts.count : 0;
			for (uint64_t i = 0; i < std::min(facts.count, kMaxChildren); ++i)
				children.push_back(NamedValue{ "[" + std::to_string(i) + "]", ValueRef{ facts.element, value.address + i * elementSize } });
			break;
		}
		case TypeShape::Pointer:
			if (const std::optional<ValueRef> target = Target(value))
			{
				if (IsObject(m_Symbols.Describe(target->type).shape)) return Children(*target);
				children.push_back(NamedValue{ "*", *target });
			}
			break;
		case TypeShape::Reference:
			if (const std::optional<ValueRef> target = Target(value)) return Children(*target);
			break;
		default: break;
		}
		return children;
	}

	std::optional<ValueRef> Values::Member(const ValueRef& object, const std::string& name) const
	{
		const std::optional<ValueRef> value = Dereferenced(object);
		if (!value || !IsObject(m_Symbols.Describe(value->type).shape)) return std::nullopt;
		const std::vector<MemberFacts> members = m_Symbols.Members(value->type);
		for (const MemberFacts& member : members)
			if (!member.isBase && member.name == name) return ValueRef{ member.type, value->address + member.offset };
		for (const MemberFacts& member : members)
			if (member.isBase)
				if (std::optional<ValueRef> found = Member(ValueRef{ member.type, value->address + member.offset }, name)) return found;
		return std::nullopt;
	}

	std::optional<ValueRef> Values::Element(const ValueRef& sequence, uint64_t index) const
	{
		const TypeFacts facts = m_Symbols.Describe(sequence.type);
		if (facts.shape == TypeShape::Array)
		{
			if (index >= facts.count) return std::nullopt;
			return ValueRef{ facts.element, sequence.address + index * (facts.size / facts.count) };
		}
		if (facts.shape == TypeShape::Pointer)
		{
			const std::optional<ValueRef> target = Target(sequence);
			if (!target) return std::nullopt;
			return ValueRef{ target->type, target->address + index * m_Symbols.Describe(target->type).size };
		}
		if (facts.shape == TypeShape::Reference)
			if (const std::optional<ValueRef> target = Target(sequence)) return Element(*target, index);
		return std::nullopt;
	}
} // namespace C4DAP::Target
