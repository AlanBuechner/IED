#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What a file's declarations declare of templates and add to them (Script/TemplateNames), by qualified name, and the
	// names an addition can mean; and which function symbols a model gives two functions, with their units
	// (SymbolTable::CollidedSymbols): what a script compile reads beyond its project's reach (docs/runtime/04 section 4).
	CLASS(GROUP = Testing) TemplateNamesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TemplateNamesTest);

		TEST_FUNC(Script.TemplateNames)
		void TemplatesAreDeclaredByTheirQualifiedNames();
		TEST_FUNC(Script.TemplateNames)
		void AdditionsAreWrittenFromTheirNamespace();
		TEST_FUNC(Script.TemplateNames)
		void AnAdditionNamesItsTemplateOutward();
		TEST_FUNC(Script.TemplateNames)
		void PlainDeclarationsDeclareNoTemplate();
		TEST_FUNC(Script.TemplateNames)
		void CollidedSymbolsKeepTheirUnits();
	};
} // namespace Testing
