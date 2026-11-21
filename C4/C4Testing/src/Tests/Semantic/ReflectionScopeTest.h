#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Which declarations runtime reflection registers (Backend/Reflection/ReflectionScope, D-89): the nearest setting
	// decides - a declaration's own [reflect], its type's, its template's, the namespace blocks around it - and what is
	// out leaves the image, the invoke and type thunks; [reflects("T")] reports a type out of scope that a serializer reads.
	CLASS(GROUP = Testing) ReflectionScopeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ReflectionScopeTest);

		TEST_FUNC(Semantic.ReflectionScope)
		void EverythingIsInByDefault();
		TEST_FUNC(Semantic.ReflectionScope)
		void TheNearestSettingDecides();
		TEST_FUNC(Semantic.ReflectionScope)
		void ANamespaceBlockCoversOnlyItself();
		TEST_FUNC(Semantic.ReflectionScope)
		void OutOfScopeLeavesTheImageAndTheThunks();
		TEST_FUNC(Semantic.ReflectionScope)
		void AnOutOfScopeGlobalHasStorageOnlyWhereNamed();
		TEST_FUNC(Semantic.ReflectionScope)
		void ASerializerOfAnOutOfScopeTypeIsAnError();
		TEST_FUNC(Semantic.ReflectionScope)
		void AMemberSerializerOfAnOutOfScopeTypeIsAnError();
		TEST_FUNC(Semantic.ReflectionScope)
		void AWrongReflectArgumentIsAnError();
	};
} // namespace Testing
