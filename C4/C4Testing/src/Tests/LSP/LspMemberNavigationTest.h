#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What a member access or a qualified name navigates to, in every feature that names a declaration: definition,
	// hover, references, highlights and rename agree.
	CLASS(GROUP = Testing) LspMemberNavigationTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspMemberNavigationTest);

		// BUG-401: a method of a class template's instance (`points.Push(1)`, `holder.ring->Push(2)`) had no declaration.
		TEST_FUNC(Lsp.Navigation.Members)
		void TestMethodsOfATemplateInstanceResolve();

		// BUG-402: a member function template (`points.Each(...)`) has no method until a call instantiates it.
		TEST_FUNC(Lsp.Navigation.Members)
		void TestMemberTemplatesResolve();

		// BUG-408: a class template's own members in its method bodies, in the class and out of line, and the template's
		// name qualifying an out-of-line definition (`Ring` of `Ring<T>::Has`).
		TEST_FUNC(Lsp.Navigation.Members)
		void TestTemplateBodiesNameTheirOwnMembers();

		// BUG-404: an enum's member through its type (`Shape::Square` in the enum's namespace, `Geo::Tone::Low`), and an
		// unscoped member by itself (`High`), is the member's declaration.
		TEST_FUNC(Lsp.Navigation.Members)
		void TestEnumMembersResolve();

		TEST_FUNC(Lsp.Navigation.Members)
		void TestEnumMembersRenameEverywhere();

		// BUG-405: a static method called through its class (`Scratch::Counter::Make(3)`).
		TEST_FUNC(Lsp.Navigation.Members)
		void TestStaticMethodsThroughTheirClassResolve();
	};
} // namespace Testing
