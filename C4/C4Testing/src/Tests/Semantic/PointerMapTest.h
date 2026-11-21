#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// R16: the pointer maps the compiler hands the collector (docs/runtime/01) - each type's in the c4_gc_type record
	// `new` passes c4_alloc_typed, each global root's in __c4_gc_roots.
	CLASS(GROUP = Testing) PointerMapTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PointerMapTest);

		TEST_FUNC(Semantic.PointerMap)
		void ARecordMapsTheWordsThatMayHoldAPointer();

		TEST_FUNC(Semantic.PointerMap)
		void AnArrayRecordMapsOneElementAfterItsCount();

		TEST_FUNC(Semantic.PointerMap)
		void ATypeOfPointersOnlyIsReadWhole();

		TEST_FUNC(Semantic.PointerMap)
		void AGlobalRootCarriesItsTypesMap();

		TEST_FUNC(Semantic.PointerMap)
		void GcTypeOfIsTheTypesMapRecord();
	};
} // namespace Testing
