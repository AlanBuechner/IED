#include "GrowOnlyContainersTest.h"
#include "Model/Concurrent/GrowOnlyList.h"
#include "Model/Concurrent/GrowOnlyMap.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		constexpr int c_Keys = 20000;
		constexpr int c_Threads = 6;
	}

	void GrowOnlyContainersTest::AMapFindsWhatWasAdded()
	{
		GrowOnlyMap<std::string, int> map;
		TEST_ASSERT(map.empty());
		TEST_ASSERT(map.find("one") == map.end());
		map.emplace("one", 1);
		map.emplace("two", 2);
		TEST_ASSERT_EQ(map.size(), size_t(2));
		TEST_ASSERT(map.count("one") == 1 && map.count("two") == 1 && map.count("three") == 0);
		TEST_ASSERT_EQ(map.find("two")->second, 2);
		map["three"] = 3;
		TEST_ASSERT_EQ(map.find("three")->second, 3);
		TEST_PASSED();
	}

	void GrowOnlyContainersTest::AddingAgainKeepsTheFirstValue()
	{
		GrowOnlyMap<int, std::string> map;
		std::string& first = map.emplace(7, "first").first->second;
		auto second = map.emplace(7, "second");
		std::string& again = second.first->second;
		TEST_ASSERT(!second.second);
		TEST_ASSERT(&first == &again);
		TEST_ASSERT_EQ(again, std::string("first"));
		TEST_ASSERT_EQ(map.size(), size_t(1));
		TEST_PASSED();
	}

	void GrowOnlyContainersTest::ValuesStayWhereTheyAreWhileTheMapGrows()
	{
		GrowOnlyMap<int, int> map;
		std::vector<int*> where;
		for (int i = 0; i < c_Keys; ++i) where.push_back(&map.emplace(i, i * 3).first->second);
		for (int i = 0; i < c_Keys; ++i)
		{
			TEST_ASSERT(&map.find(i)->second == where[i]);
			TEST_ASSERT_EQ(*where[i], i * 3);
		}
		TEST_ASSERT(map.find(c_Keys) == map.end());
		TEST_PASSED();
	}

	void GrowOnlyContainersTest::AMapVisitsItsEntriesInTheOrderAdded()
	{
		GrowOnlyMap<int, int> map;
		for (int i = 0; i < 100; ++i) map.emplace(99 - i, i);
		int next = 0;
		bool inOrder = true;
		for (const auto& entry : map) { inOrder = inOrder && entry.first == 99 - next && entry.second == next; ++next; }
		TEST_ASSERT(inOrder);
		TEST_ASSERT_EQ(next, 100);
		TEST_PASSED();
	}

	void GrowOnlyContainersTest::AMovedMapKeepsItsEntries()
	{
		GrowOnlyMap<int, int> map;
		for (int i = 0; i < 50; ++i) map.emplace(i, i + 1);
		const int* seven = &map.find(7)->second;
		GrowOnlyMap<int, int> moved = std::move(map);
		TEST_ASSERT(&moved.find(7)->second == seven);
		TEST_ASSERT_EQ(moved.size(), size_t(50));
		moved[50] = 51;
		TEST_ASSERT_EQ(moved.find(50)->second, 51);
		TEST_PASSED();
	}

	// A reader asks only for keys the writer has said are in: each must be found, with its value, at once.
	void GrowOnlyContainersTest::ReadersFindEveryKeyWhileAWriterAdds()
	{
		GrowOnlyMap<int, int> map;
		std::atomic<int> added{ 0 };
		std::atomic<int> missed{ 0 };
		std::vector<std::thread> readers;
		for (int t = 0; t < c_Threads; ++t)
			readers.emplace_back([&, t] {
				unsigned seed = 12345u + static_cast<unsigned>(t);
				while (added.load(std::memory_order_acquire) < c_Keys)
				{
					const int known = added.load(std::memory_order_acquire);
					if (known == 0) continue;
					seed = seed * 1664525u + 1013904223u;
					const int key = static_cast<int>(seed % static_cast<unsigned>(known));
					const auto found = map.find(key);
					if (found == map.end() || found->second != key * 2) missed.fetch_add(1);
				}
			});
		for (int i = 0; i < c_Keys; ++i)
		{
			map.emplace(i, i * 2);
			added.store(i + 1, std::memory_order_release);
		}
		for (std::thread& reader : readers) reader.join();
		TEST_ASSERT_EQ(missed.load(), 0);
		TEST_ASSERT_EQ(map.size(), size_t(c_Keys));
		TEST_PASSED();
	}

	void GrowOnlyContainersTest::SeveralWritersAddEachKeyOnce()
	{
		GrowOnlyMap<int, int> map;
		std::vector<std::thread> writers;
		for (int t = 0; t < c_Threads; ++t)
			writers.emplace_back([&, t] { for (int i = 0; i < c_Keys; ++i) map.emplace(i, t); });
		for (std::thread& writer : writers) writer.join();
		TEST_ASSERT_EQ(map.size(), size_t(c_Keys));
		int visited = 0;
		for (const auto& entry : map) visited += entry.first >= 0 ? 1 : 0;
		TEST_ASSERT_EQ(visited, c_Keys);
		TEST_PASSED();
	}

	void GrowOnlyContainersTest::AListKeepsItsOrderAndItsElements()
	{
		GrowOnlyList<std::string> list;
		TEST_ASSERT(list.empty());
		std::string& first = list.push_back("a");
		list.push_back("b");
		list.push_back("c");
		std::string joined;
		for (const std::string& element : list) joined += element;
		TEST_ASSERT_EQ(joined, std::string("abc"));
		TEST_ASSERT(&list.front() == &first);
		GrowOnlyList<std::string> moved = std::move(list);
		TEST_ASSERT_EQ(moved.size(), size_t(3));
		TEST_ASSERT(&moved.front() == &first);
		TEST_PASSED();
	}

	// A walk sees a prefix of what is added, in order, never a torn element.
	void GrowOnlyContainersTest::AListIsWalkedWhileItGrows()
	{
		GrowOnlyList<int> list;
		std::atomic<bool> done{ false };
		std::atomic<int> outOfOrder{ 0 };
		std::vector<std::thread> walkers;
		for (int t = 0; t < c_Threads; ++t)
			walkers.emplace_back([&] {
				while (!done.load(std::memory_order_acquire))
				{
					int expected = 0;
					for (int element : list)
						if (element != expected++) outOfOrder.fetch_add(1);
				}
			});
		for (int i = 0; i < c_Keys; ++i) list.push_back(i);
		done.store(true, std::memory_order_release);
		for (std::thread& walker : walkers) walker.join();
		TEST_ASSERT_EQ(outOfOrder.load(), 0);
		TEST_ASSERT_EQ(list.size(), size_t(c_Keys));
		TEST_PASSED();
	}
} // namespace Testing
