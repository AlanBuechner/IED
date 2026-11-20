#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Server/CompilationSession.h"
#include "Features/SemanticTokens.h"
#include "LspTestSupport.h"

namespace Testing
{
	// Semantic-highlighting (textDocument/semanticTokens) against in-memory C4 sources. Each test
	// compiles a small program through the real frontend and checks that the emitted tokens carry
	// the standard C++-like kind for the right spans.
	CLASS(GROUP = Testing) LspSemanticTokensTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspSemanticTokensTest);

		// --- Legend / capability shape ---
		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestTokenTypeNamesMatchLegendOrder();

		// --- Kind classification ---
		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestBuiltinTypeKeywordIsTypeKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestBuiltinTypesGetModifierButUserDefinedDoNot();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestGlobalVariableDeclarationIsVariableWithDefinition();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestStructNameIsStructKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestEnumNameIsEnumKindAndMembersAreVariables();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestFreeFunctionDefinitionIsFunctionKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestMethodInsideStructIsMethodKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestFieldDeclarationIsFieldKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestFunctionParameterIsVariableKind();

		// --- References (uses, not declarations) ---
		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestUseOfGlobalVariableIsVariableWithoutDefinition();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestCallToFreeFunctionIsFunctionKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestMemberAccessFieldReferenceIsFieldKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestMethodCalledThroughAFieldIsMethodKind();

		// --- Keywords / modifiers / literals / comments ---
		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestControlFlowKeywordsAreControlFlowKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestAccessModifiersAndQualifiersAreKeywordKind();

		// BUG-464: modifiers the parser commits by their text (`inline`, `abstract`, `final`, `noexcept`, `volatile`,
		// `thread_local`, `packed`, extern's `source` and its block's `define` / `include_dir` / `lib`, `sizeof` and
		// the casts) are keywords where they carry that meaning; a local named `final` stays a variable.
		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestContextualModifiersAreKeywordKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestNumberLiteralIsNumberKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestCommentIsCommentKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestTokensNeverCrossALine();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestStringLiteralIsStringKind();

		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestIncludeDirectiveIsPreprocessorKind();

		// --- Unknown / empty document ---
		TEST_FUNC(Lsp.Features.SemanticTokens)
		void TestUnknownDocumentReturnsNoTokens();

	// --- Qualified chains (namespace/owner prefixes keep their declaration kinds) ---
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestQualifiedChainSegmentsKeepDeclarationKinds();

	// A type inside a namespace keeps its own kind: `Engine::Thing` must read as struct, not namespace.
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestTypeInsideNamespaceKeepsItsOwnKindInChains();

	// Preprocessor lines: directive keywords stay in the neutral keyword family; macro NAMES are purple at their
	// definition AND every usage site (this file and other files).
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestPreprocessorDirectivesArePreprocessorAndMacroNamesMacro();

	// Cross-file: a #define in one source colors usages of its name in another source as macros.
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestMacroNameColorsUsageInAnotherFile();

	// Template instantiation arguments get real type coloring instead of plain variable: `Box<int32>`'s int32 is a
	// language primitive (type + builtin), `Box<Vec2>`'s Vec2 keeps its struct kind, and the template parameter T in
	// `Box<T>` reads as typeParameter; so do nested arguments (`Box<Box<Vec2>>`) and a call's (`Make<Vec2>()`).
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestTemplateInstantiationArgumentsKeepTypeColors();

	// Swizzle lane views (`packed float x, y;` inside a class union): the `packed` / `unpacked` keyword is lexed as an
	// identifier and stored on no AST name node, so it must be recorded explicitly by the walk (kind Keyword), the lane
	// names read as field definitions, and the element type keeps its real QualType classification (primitive -> builtin).
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestSwizzleViewPackedKeywordLanesAndElementType();

	// BUG-329: `char` of `const char*` and `uint8` of `uint8[4]` read as built in, like a bare primitive.
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestPrimitiveBehindPointerOrArrayIsBuiltin();

	// BUG-330 / BUG-333: the type name of a constructor call keeps its own kind (struct), not variable.
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestConstructorCallNamesItsType();

	// BUG-331: a method called without `this.` inside another method is a method, as through `.`.
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestImplicitThisCallIsMethod();

	// BUG-334: an inherited static field reached through `Derived::` is a field, not a method.
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestInheritedStaticFieldThroughOwnerIsField();

	// BUG-335: a scoped enum constant through `Owner::` is a variable (an enum constant), as unscoped ones are.
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestScopedEnumConstantThroughOwnerIsVariable();

	// A unit's tree holds its own file alone (no include splice): what the header it includes declares - a field used
	// bare in a method defined in the .cf, a type nested in a class named as an owner - still reads with its own kind.
	TEST_FUNC(Lsp.Features.SemanticTokens)
	void TestAnIncludedHeadersFieldsAndTypesKeepTheirKinds();
	};
} // namespace Testing
