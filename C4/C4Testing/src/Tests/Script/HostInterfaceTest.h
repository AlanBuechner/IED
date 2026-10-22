#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The `.c4i` format (Script/HostInterface): every field round-trips, the host's target included; another format,
	// another version, a short file, trailing bytes and an unknown target are refused with a reason.
	CLASS(GROUP = Testing) HostInterfaceTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::HostInterfaceTest);

		TEST_FUNC(Script.HostInterface)
		void EveryFieldRoundTrips();
		TEST_FUNC(Script.HostInterface)
		void AnotherFormatIsRefused();
		TEST_FUNC(Script.HostInterface)
		void AnotherVersionIsRefused();
		TEST_FUNC(Script.HostInterface)
		void AShortFileIsRefused();
		TEST_FUNC(Script.HostInterface)
		void AnUnknownTargetIsRefused();
		TEST_FUNC(Script.HostInterface)
		void TrailingBytesAreRefused();
		TEST_FUNC(Script.HostInterface)
		void WritesAndReadsAFile();
		TEST_FUNC(Script.HostInterface)
		void AMissingFileIsAnError();
		TEST_FUNC(Script.HostInterface)
		void ASealedInterfaceCarriesItsBuildId();
		TEST_FUNC(Script.HostInterface)
		void AnUnchangedInterfaceIsNotWrittenAgain();
	};
} // namespace Testing
