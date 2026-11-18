#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// `[annotation] struct Requires {}` declares the annotation `[Requires]` (docs/language/12 section 4).
	constexpr const char* kAnnotationDeclaration = "annotation";

	/// Represents a single annotation (attribute) within a bracket group.
	///
	/// An annotation is a qualified name optionally followed by parenthesized arguments.
	/// Examples: `Serialize`, `Test("stuff")`, `NC::SomeAttr(1, 2)`
	struct AnnotationItem : public AstNode
	{
		AnnotationItem() = default;

		// --- Getters ---

		/// Qualified name tokens (identifier components separated by ::).
		const Core::Vector<Token>& GetNameTokens() const { return m_NameTokens; }

		/// Full qualified name as a string (e.g. "NC::SomeAttr").
		const Core::String& GetName() const { return m_Name; }
		/// The name's last part, the one a declared annotation is named by (`SomeAttr` of `NC::SomeAttr`).
		Core::String GetOwnName() const;

		/// Number of identifier components in the qualified name.
		uint32 GetNameTokenCount() const { return m_NameTokens.Count(); }

		/// Optional parenthesized arguments, empty if none provided: each an expression, or a type when its tokens read only as
		/// one (`int32*`).
		const Core::Vector<Ref<AstNode>>& GetArguments() const { return m_Arguments; }
		/// Argument `index` read as a type when its tokens read as one (`Render::MeshRenderer`, `Box<int32>`, `int32*`), else
		/// null: a name is a constant or a type, which the model decides. An argument that reads only as a type is this node.
		const Ref<QualTypeNode>& GetArgumentType(uint32 index) const { return m_ArgumentTypes[index]; }

		/// One of the compiler's own annotations (operator, test, test_setup, test_teardown, group, link, uuid, target, annotation):
		/// its arguments are string literals and reflection does not keep it (D-42).
		bool IsCompilerAnnotation() const;
		/// The compiler annotation this unknown name is a near miss of (`operater`), or null.
		const char* NearCompilerAnnotation() const;

		std::span<const Ref<AstNode>> Children() const override { return m_Arguments; }

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::AnnotationItem; }

	private:
		Core::Vector<Token> m_NameTokens;
		Core::String m_Name;
		Core::Vector<Ref<AstNode>> m_Arguments;
		Core::Vector<Ref<QualTypeNode>> m_ArgumentTypes; // one per argument

		friend struct AnnotationNode; // allow AnnotationNode to populate fields during parsing
	};

	/// Represents one or more consecutive annotation bracket groups decorating a declaration.
	///
	/// Example: `[Serialize, Test("stuff")][AnotherAttr]` is a single AnnotationNode
	/// containing two BracketGroups with three total AnnotationItems.
	struct AnnotationNode : public AstNode
	{
		struct BracketGroup
		{
			Token openBracketToken;
			Core::Vector<Ref<AnnotationItem>> annotations;
			Token closeBracketToken;
		};

		AnnotationNode() = default;

		// --- Getters ---

		const Core::Vector<BracketGroup>& GetBracketGroups() const { return m_BracketGroups; }

		/// Total number of annotation items across all bracket groups.
		uint32 TotalAnnotationCount() const;

		static Ref<AnnotationNode> TryParse(ParserState& state);

		/// The kind of the token after the annotations and the access modifier ahead (`enum` in
		/// `[Flags] public enum ...`); the lexer stays where it was.
		static TokenType PeekPastAnnotations(ParserState& state);

		void ParseBody(ParserState& state);

		std::span<const Ref<AstNode>> Children() const override; // flatten all items

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::Annotation; }

	private:
		Core::Vector<BracketGroup> m_BracketGroups;
		Core::Vector<Ref<AstNode>> m_FlattenedChildren; // flattened items for Children()

		void ParseAnnotation(ParserState& state, BracketGroup& group);
		static void ParseArguments(ParserState& state, AnnotationItem& item);
		static bool ParseArgument(ParserState& state, AnnotationItem& item);
		static void CheckCompilerArguments(ParserState& state, const AnnotationItem& item);
		void ParseSingleBracketGroup(ParserState& state); // parses exactly one [ ... ] group
	};
}
