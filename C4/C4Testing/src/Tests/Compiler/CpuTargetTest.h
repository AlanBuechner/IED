#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The build's processor (Compiler/CpuTarget, D-62): -march and -m / -mno- flags with LLVM's implications, the
	// SIMD_* macros, the LLVM round trip a script host records, and the errors for flags that select nothing.
	CLASS(GROUP = Testing) CpuTargetTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CpuTargetTest);

		TEST_FUNC(Compiler.CpuTarget)
		void TheBaselineIsSse2();
		TEST_FUNC(Compiler.CpuTarget)
		void MarchSelectsTheCpusFeatures();
		TEST_FUNC(Compiler.CpuTarget)
		void AFeatureTurnsOnWhatItNeeds();
		TEST_FUNC(Compiler.CpuTarget)
		void TurningAFeatureOffTurnsOffWhatNeedsIt();
		TEST_FUNC(Compiler.CpuTarget)
		void TheLastFlagWins();
		TEST_FUNC(Compiler.CpuTarget)
		void Avx512HoldsSixteenFloats();
		TEST_FUNC(Compiler.CpuTarget)
		void NativeIsTheBuildingMachine();
		TEST_FUNC(Compiler.CpuTarget)
		void UnknownCpusAndFeaturesAreErrors();
		TEST_FUNC(Compiler.CpuTarget)
		void TheLlvmFormRoundTrips();
		TEST_FUNC(Compiler.CpuTarget)
		void AMalformedLlvmFormIsAnError();
		TEST_FUNC(Compiler.CpuTarget)
		void TheCommandLineSelectsTheTarget();
		TEST_FUNC(Compiler.CpuTarget)
		void PredefinedMacrosCarryTheTarget();
	};
} // namespace Testing
