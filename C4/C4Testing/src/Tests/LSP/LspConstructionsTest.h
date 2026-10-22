#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// A class and its constructor across files, as IED's LspPool is (declared in a header, built by `new` in another
	// file): references to the class include every place a type is written (a field in another header, `new T`, a
	// local, a temporary) and the qualifier of each out-of-line definition; references to a create operator include
	// the constructions that call it, which rename leaves alone and call hierarchy shows as calls.
	CLASS(GROUP = Testing) LspConstructionsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspConstructionsTest);

		TEST_FUNC(Lsp.Features.Constructions)
		void TestReferencesOfAClassIncludeEveryWrittenUse();

		TEST_FUNC(Lsp.Features.Constructions)
		void TestReferencesOfAConstructorAreItsConstructions();

		TEST_FUNC(Lsp.Features.Constructions)
		void TestRenameKeepsConstructionsAndRenamesQualifiers();

		TEST_FUNC(Lsp.Features.Constructions)
		void TestConstructionsAreCallsOfTheConstructor();

		TEST_FUNC(Lsp.Features.Constructions)
		void TestTheTypeOfANewGoesToItsClass();

		TEST_FUNC(Lsp.Features.Constructions)
		void TestAProjectOnDiskFindsTheFieldInAnotherHeader();

		TEST_FUNC(Lsp.Features.Constructions)
		void TestATypeWrittenInADeclarationIsItsClass();
	};
} // namespace Testing
