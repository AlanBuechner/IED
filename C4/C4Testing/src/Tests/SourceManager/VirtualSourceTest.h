#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Sources registered in memory under a path of their own (a script's interface, a test's host): found by every lookup,
	// which spells a path lexically normal, on every system (BUG-493).
	CLASS(GROUP = Testing) VirtualSourceTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::VirtualSourceTest);

		TEST_FUNC(SourceManager.Virtual)
		void AVirtualSourceIsFoundByItsNormalPath();
		TEST_FUNC(SourceManager.Virtual)
		void AnIncludeBesideAVirtualSourceIsFound();
		TEST_FUNC(SourceManager.Virtual)
		void AVirtualFolderHoldsItsProjectsFiles();
	};
} // namespace Testing
