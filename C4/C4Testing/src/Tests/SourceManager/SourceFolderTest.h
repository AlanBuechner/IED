#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Registering a folder's sources: names in any script, as UTF-8 (BUG-300).
	CLASS(GROUP = Testing) SourceFolderTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::SourceFolderTest);

		TEST_FUNC(SourceManager.Folders)
		void NamesOutsideTheCodePageAreUtf8();
		TEST_FUNC(SourceManager.Folders)
		void OnlyC4SourcesAreRegistered();
	};
} // namespace Testing
