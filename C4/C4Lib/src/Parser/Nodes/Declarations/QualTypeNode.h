#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// A single indirection layer on a type: pointer `*` (always GC-managed), C++ ref `&`, or array `[expr]`.
	struct IndirectionLayer
	{
		bool isConst = false;
		TokenType indirectionToken = TokenType::None;
		Ref<AstNode> arraySizeExpr;
		Token constToken;
		Token indirectionStartToken;
		Token closingBracketToken;
	};

	/// Parses a qualified type specification: optional leading const, namespace-qualified name with template args,
	/// and trailing indirection layers. Also handles delegate types like `delegate<void(int)>`.
	///
	/// Grammar: `[const] (identifier | delegate_type) [indirection]*`
	/// Examples: `int`, `const int*`, `NC::SomeClass<float>::SubType&[10+N]`, `delegate<void(int code)>`
	struct QualTypeNode : public AstNode
	{
		QualTypeNode() = default;

		bool IsLeadingConst() const { return m_IsLeadingConst; }
		const Token& GetLeadingConstToken() const { return m_LeadingConstToken; }
		/// `packed T[N]` (D-36): the array is one SIMD vector value.
		bool IsPacked() const { return m_IsPacked; }
		const Token& GetPackedToken() const { return m_PackedToken; }
		/// Whether any layer is an array (`packed` needs one).
		bool HasArrayLayer() const;
		const Ref<NamespaceQualifiedNameNode>& BaseName() const { return m_BaseName; }
		const Ref<AstNode>& FuncPtrType() const { return m_DelegateType; } // FunPtrTypeNode when funcptr type
		uint32 IndirectionCount() const { return m_Indirections.Count(); }
		const Core::Vector<IndirectionLayer>& GetIndirections() const { return m_Indirections; }

		/// Template arguments for a specialization (e.g. `Vec<int>` -> [int]). The base-name node
		/// (NamespaceQualifiedNameNode) parses `<...>` onto the FINAL segment of the qualified name,
		/// so `Math::Box<int32>` carries its args on the last segment, not the first. These accessors
		/// delegate to that final segment. Empty when the type is not a specialization.
		bool HasTemplateArgs() const { // `Buf<>` counts: a template use whose arguments are all defaulted
			if (!m_BaseName || m_BaseName->SegmentCount() == 0) return false;
			return m_BaseName->GetSegments().Back().templateArgs != nullptr;
		}
		uint32 TemplateArgCount() const {
			if (!HasTemplateArgs()) return 0;
			return m_BaseName->GetSegments().Back().templateArgs->ArgumentCount();
		}
		const Ref<AstNode>& GetTemplateArg(uint32 index) const {
			return m_BaseName->GetSegments().Back().templateArgs->GetArguments()[index];
		}

		/// Build a type node from already-known parts without re-reading the lexer. Used by template
		/// instantiation to produce a substituted type (a bound parameter rewritten to its concrete
		/// name) where the tokens are not available to parse. Mirrors NamespaceQualifiedNameNode::FromParts.
		static Ref<QualTypeNode> FromParts(
			const Ref<NamespaceQualifiedNameNode>& baseName,
			const Ref<AstNode>& delegateType,
			const Core::Vector<IndirectionLayer>& indirections,
			bool leadingConst);

		static Ref<QualTypeNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

		/// A return list `[float, bool]` (D-34): the types of the values a function returns together. It is written
		/// only as a return type, and a node that is one has no base name, signature type or layers.
		bool IsReturnList() const { return !m_ReturnList.Empty(); }
		const Core::Vector<Ref<QualTypeNode>>& GetReturnList() const { return m_ReturnList; }
		/// An element of a return list written `Ts...`: one element per element of the packs it names (D-19).
		bool ExpandsPack() const { return m_ExpandsPack; }
		static Ref<QualTypeNode> ParseReturnList(ParserState& state);
		/// A return type: a return list when `[` comes next, else a type.
		static Ref<QualTypeNode> ParseReturnType(ParserState& state);
		/// Whether a return list followed by a function's name and `(` is ahead, as opposed to an annotation group
		/// (`[test("a")] void F()`).
		static bool StartsReturnList(const Lexer& lexer);

		// A declaration's modifier loop consumes a leading `const` before the type node parses; it is
		// handed back here, so this node alone says what is const (D-30).
		void SetLeadingConstFrom(const Token& tok) { m_IsLeadingConst = true; m_LeadingConstToken = tok; }

		/// The declared entity (a variable, field or parameter) is read-only: the leading `const` when the
		/// type has no pointer layer, or a `const` written before the outermost `*` (`int32 const* p` is a
		/// const pointer). An array of const elements is a const array; a reference is never const itself.
		bool DeclaresConstValue() const;

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::QualType; }

	private:
		Core::Vector<Ref<QualTypeNode>> m_ReturnList;
		bool m_ExpandsPack = false;
		bool m_IsLeadingConst = false;
		Token m_LeadingConstToken;
		bool m_IsPacked = false;
		Token m_PackedToken;
		Ref<NamespaceQualifiedNameNode> m_BaseName;
		Ref<AstNode> m_DelegateType; // FunPtrTypeNode when this is a funcptr type
		Core::Vector<IndirectionLayer> m_Indirections;
		Core::Vector<Ref<AstNode>> m_Children;
	};
}
