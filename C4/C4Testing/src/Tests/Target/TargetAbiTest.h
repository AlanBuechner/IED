#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What each target answers (Target/TargetAbi.h, R11): one implementation per system, picked once per build.
	CLASS(GROUP = Testing) TargetAbiTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TargetAbiTest);

		TEST_FUNC(Target.Abi)
		void EachSystemHasOneTarget();
		TEST_FUNC(Target.Abi)
		void ABuildPicksItsSystemsTarget();
		TEST_FUNC(Target.Abi)
		void WindowsIsMsvcShaped();
		TEST_FUNC(Target.Abi)
		void LinuxIsSystemVAndElf();
		TEST_FUNC(Target.Abi)
		void DataLayoutsDifferOnlyInMangling();
		TEST_FUNC(Target.Abi)
		void LinuxModulesArePositionIndependent();
		TEST_FUNC(Target.Abi)
		void OnlyElfAnchorsTablesAtTheirFields();
		TEST_FUNC(Target.Abi)
		void OnlyCodeViewLinksADebugFile();
		TEST_FUNC(Target.Abi)
		void ScriptHostsLinkTheirSystemsLibrary();
		TEST_FUNC(Target.Abi)
		void ForeignSourcesTakeTheSysroot();
		TEST_FUNC(Target.Abi)
		void EachTargetBuildsItsOwnRuntimeFiles();
		TEST_FUNC(Target.Abi)
		void TheInstallLinksEveryTarget();
	};
} // namespace Testing
