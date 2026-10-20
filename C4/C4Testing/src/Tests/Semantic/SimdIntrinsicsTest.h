#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The SIMD intrinsics and packed values (D-62) in the emitted module: each family's LLVM form, the fixed order of
	// float reductions, the fma feature gate, the build's target on every function, lane-wise and bit casts, and a
	// scalar broadcast that evaluates its operand once (BUG-250).
	CLASS(GROUP = Testing) SimdIntrinsicsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::SimdIntrinsicsTest);

		TEST_FUNC(Semantic.SimdIntrinsics)
		void LaneMathLowersToLlvmIntrinsics();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void FloatMinAndMaxCompareAndSelect();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void FloatReductionsAreAHalvingTree();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void IntegerReductionsUseLlvmReductions();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void ShuffleIndicesAreTheShuffleMask();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void MemoryIntrinsicsUseMaskedAccesses();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void FmaNeedsTheFmaFeature();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void EveryDefinitionCarriesTheTarget();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void PackedCastsConvertLaneByLane();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void ABroadcastEvaluatesItsScalarOnce();
		TEST_FUNC(Semantic.SimdIntrinsics)
		void AProgramFunctionWinsOverAnIntrinsic();
	};
} // namespace Testing
