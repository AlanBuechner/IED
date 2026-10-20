#pragma once
#include "Target/Process.h"
#include "Target/Symbols.h"
#include <optional>
#include <string>
#include <vector>

namespace C4DAP::Target
{
	/// A value in the debuggee: its type and where it is, in memory or (optimized code) in a register.
	struct ValueRef
	{
		TypeRef type;
		uint64_t address = 0;
		bool inRegister = false;
		uint64_t registerBits = 0;
	};

	struct NamedValue
	{
		std::string name;
		ValueRef value;
	};

	/// The debuggee's values shown by their types (docs/tools/03-editor-lsp-dap.md, "Values").
	class Values
	{
	public:
		Values(const Process& process, const Symbols& symbols) : m_Process(process), m_Symbols(symbols) {}

		std::string Text(const ValueRef& value) const;
		std::string TypeName(TypeRef type) const;
		bool HasChildren(const ValueRef& value) const;
		/// An object's bases and fields, an array's elements, what a pointer points at.
		std::vector<NamedValue> Children(const ValueRef& value) const;

		/// What a pointer or reference points at; none for null or a `void*`.
		std::optional<ValueRef> Target(const ValueRef& pointer) const;
		/// A field of an object (or of an object a pointer or reference leads to), searched through its bases.
		std::optional<ValueRef> Member(const ValueRef& object, const std::string& name) const;
		/// An element of an array, or of what a pointer points at.
		std::optional<ValueRef> Element(const ValueRef& sequence, uint64_t index) const;

	private:
		bool Bytes(const ValueRef& value, uint64_t size, std::vector<uint8_t>& bytes) const;
		std::optional<uint64_t> PointerValue(const ValueRef& pointer) const;
		std::string Summary(const ValueRef& value, int depth) const;
		std::string EnumText(const ValueRef& value, const TypeFacts& facts) const;
		std::string PointerText(const ValueRef& value, const TypeFacts& facts) const;
		std::string ObjectSummary(const ValueRef& value, int depth) const;
		std::string ArraySummary(const ValueRef& value, const TypeFacts& facts, int depth) const;
		std::optional<ValueRef> Dereferenced(const ValueRef& value) const;

		const Process& m_Process;
		const Symbols& m_Symbols;
	};
} // namespace C4DAP::Target
