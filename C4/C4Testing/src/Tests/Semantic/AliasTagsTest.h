#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Strict aliasing (D-64) in the emitted module: the !tbaa class of each access (by size for integers, bytes for
	// the one-byte types, a union member of several classes and a mixed object), SIMD memory, the flag that turns it
	// off, and reinterpret_cast between numbers.
	CLASS(GROUP = Testing) AliasTagsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::AliasTagsTest);

		TEST_FUNC(Semantic.AliasTags)
		void ScalarsTakeTheClassOfTheirSize();
		TEST_FUNC(Semantic.AliasTags)
		void ObjectsTakeTheirFieldsClass();
		TEST_FUNC(Semantic.AliasTags)
		void UnionMembersOfSeveralClassesAreBytes();
		TEST_FUNC(Semantic.AliasTags)
		void AnElementOfAUnionArrayIsBytes();
		TEST_FUNC(Semantic.AliasTags)
		void PackedLanesTakeTheirElementsClass();
		TEST_FUNC(Semantic.AliasTags)
		void SimdMemoryTakesTheLanesClass();
		TEST_FUNC(Semantic.AliasTags)
		void PointersInAStructArePointers();
		TEST_FUNC(Semantic.AliasTags)
		void NoStrictAliasingTagsNothing();
		TEST_FUNC(Semantic.AliasTags)
		void ReinterpretCastKeepsANumbersBits();
	};
} // namespace Testing
