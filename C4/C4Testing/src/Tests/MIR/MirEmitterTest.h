#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Backend/CodeGen/MirEmitter and the EhLowering of each target (R5, R11): one MIR, emitted as MSVC funclets for
	// Windows and as landing pads for Linux; a block that is its predecessor's only way on adds no branch.
	CLASS(GROUP = Testing) MirEmitterTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MirEmitterTest);

		TEST_FUNC(Mir.Emitter)
		void WindowsUnwindsThroughFunclets();
		TEST_FUNC(Mir.Emitter)
		void LinuxUnwindsThroughLandingPads();
		TEST_FUNC(Mir.Emitter)
		void ANoexceptBodyStopsTheProgramOnBoth();
		TEST_FUNC(Mir.Emitter)
		void StraightLineCodeIsOneBlock();
	};
} // namespace Testing
