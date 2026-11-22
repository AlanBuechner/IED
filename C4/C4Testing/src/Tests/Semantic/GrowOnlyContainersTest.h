#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The containers the model's lowering-time tables are (Model/Concurrent): added to by one thread at a time while
	// others read without a lock.
	CLASS(GROUP = Testing) GrowOnlyContainersTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::GrowOnlyContainersTest);

		TEST_FUNC(Semantic.GrowOnlyContainers)
		void AMapFindsWhatWasAdded();
		TEST_FUNC(Semantic.GrowOnlyContainers)
		void AddingAgainKeepsTheFirstValue();
		TEST_FUNC(Semantic.GrowOnlyContainers)
		void ValuesStayWhereTheyAreWhileTheMapGrows();
		TEST_FUNC(Semantic.GrowOnlyContainers)
		void AMapVisitsItsEntriesInTheOrderAdded();
		TEST_FUNC(Semantic.GrowOnlyContainers)
		void AMovedMapKeepsItsEntries();
		TEST_FUNC(Semantic.GrowOnlyContainers)
		void ReadersFindEveryKeyWhileAWriterAdds();
		TEST_FUNC(Semantic.GrowOnlyContainers)
		void SeveralWritersAddEachKeyOnce();
		TEST_FUNC(Semantic.GrowOnlyContainers)
		void AListKeepsItsOrderAndItsElements();
		TEST_FUNC(Semantic.GrowOnlyContainers)
		void AListIsWalkedWhileItGrows();
	};
} // namespace Testing
