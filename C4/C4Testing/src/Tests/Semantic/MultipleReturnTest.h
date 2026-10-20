#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Return lists in the model (D-34): one anonymous C-layout struct per list of element types, resolved in a
	// template's bindings and expanding packs; and the HIR of a destructuring.
	CLASS(GROUP = Testing) MultipleReturnTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MultipleReturnTest);

		TEST_FUNC(Semantic.ReturnLists)
		void ListTypesAreInternedStructs();
		TEST_FUNC(Semantic.ReturnLists)
		void TemplateListResolvesInBindings();
		TEST_FUNC(Semantic.ReturnLists)
		void PackExpandsInAList();
		TEST_FUNC(Semantic.ReturnLists)
		void DestructuringMovesObjectsOut();
	};
} // namespace Testing
