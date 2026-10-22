#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	struct ParserState;

	/// Braced initializer expression: {expr, expr, ...}
	/// Used for struct initialization, array initialization, and return Type{args};
	struct BracedInitializerNode : public AstNode
	{
		static constexpr NodeType kNodeType = NodeType::BracedInitializer;
		BracedInitializerNode() = default;

		const Core::Vector<Ref<AstNode>>& GetElements() const { return m_Elements; }
		uint32 ElementCount() const { return m_Elements.Count(); }
		struct Entry {
			Ref<AstNode> m_Key; // key expression ([i], bare name, or literal); null for positional
			bool m_IsBracketed = false; // `[key] = value` rather than `key = value`
			bool m_IsRange = false;
			Ref<AstNode> m_RangeEnd; // present when m_IsRange
			Ref<AstNode> m_Value;
		};
		const Core::Vector<Entry>& GetEntries() const { return m_Entries; }
		uint32 EntryCount() const { return m_Entries.Count(); }
		/// True when an entry names its target (`[i] = v`, `[a...b] = v`, `field = v`).
		bool HasDesignators() const
		{
			for (const Entry& e : m_Entries)
				if (e.m_Key) return true;
			return false;
		}

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
NodeType Type() const override { return NodeType::BracedInitializer; }

		void ParseBody(ParserState& state);
		void ParseOneEntry(ParserState& state); // one positional element or [key]/key = value entry

	private:
		Core::Vector<Ref<AstNode>> m_Elements;
		Core::Vector<Entry> m_Entries; // every entry in source order, positional ones with no key
		Token m_OpenBraceToken;
		Token m_CloseBraceToken;
	};
}
