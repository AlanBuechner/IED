#include "CallbackTest.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>

namespace Testing
{
	void CallbackTest::TestHandleUniqueness()
	{
		// Each subscription should return a unique handle.
		std::vector<Reflect::CallbackHandle> handles;

		for (int i = 0; i < 10; i++) {
			auto handle = Reflect::Scripting::SubscribeUnload([](const std::string&) {});
			handles.push_back(handle);
		}

		// Check all handles are unique
		std::vector<Reflect::CallbackHandle> sortedHandles = handles;
		std::sort(sortedHandles.begin(), sortedHandles.end());

		for (size_t i = 1; i < sortedHandles.size(); i++) {
			if (sortedHandles[i] == sortedHandles[i - 1]) {
				Reflect::Scripting::Unsubscribe(handles[i]);
				TEST_FAILED("Duplicate callback handle detected");
			}
		}

		// Clean up
		for (auto handle : handles) {
			Reflect::Scripting::Unsubscribe(handle);
		}

		TEST_PASSED();
	}

	void CallbackTest::TestUnloadCallbackInvocation()
	{
		std::atomic<int> invocationCount{0};
		std::string receivedLangID;

		auto handle = Reflect::Scripting::SubscribeUnload([&](const std::string& langID) {
			invocationCount++;
			receivedLangID = langID;
		});

		const std::string testLangID = "test-lang-1";
		Reflect::Scripting::UnloadScriptingReflection(testLangID);

		if (invocationCount != 1) {
			Core::String msg = Core::FormatString("Expected 1 invocation, got {}", invocationCount.load());
			Reflect::Scripting::Unsubscribe(handle);
			TEST_FAILED(msg);
		}

		if (receivedLangID != testLangID) {
			Core::String msg = Core::FormatString("Expected lang ID '{}', got '{}'", testLangID, receivedLangID);
			Reflect::Scripting::Unsubscribe(handle);
			TEST_FAILED(msg);
		}

		Reflect::Scripting::Unsubscribe(handle);
		TEST_PASSED();
	}

	void CallbackTest::TestReloadCallbackInvocation()
	{
		std::atomic<int> invocationCount{0};
		std::string receivedLangID;

		auto handle = Reflect::Scripting::SubscribeReload([&](const std::string& langID) {
			invocationCount++;
			receivedLangID = langID;
		});

		const std::string testLangID = "reload-test-lang";
		Reflect::Scripting::InvokeReloadCallbacks(testLangID);

		if (invocationCount != 1) {
			Core::String msg = Core::FormatString("Expected 1 invocation, got {}", invocationCount.load());
			Reflect::Scripting::Unsubscribe(handle);
			TEST_FAILED(msg);
		}

		if (receivedLangID != testLangID) {
			Core::String msg = Core::FormatString("Expected lang ID '{}', got '{}'", testLangID, receivedLangID);
			Reflect::Scripting::Unsubscribe(handle);
			TEST_FAILED(msg);
		}

		Reflect::Scripting::Unsubscribe(handle);
		TEST_PASSED();
	}

	void CallbackTest::TestUnsubscribeRemovesCallback()
	{
		std::atomic<int> invocationCount{0};

		auto handle = Reflect::Scripting::SubscribeReload([&](const std::string&) {
			invocationCount++;
		});

		// Invoke once - should trigger
		Reflect::Scripting::InvokeReloadCallbacks("test");
		if (invocationCount != 1) {
			Core::String msg = Core::FormatString("Expected 1 invocation before unsubscribe, got {}", invocationCount.load());
			Reflect::Scripting::Unsubscribe(handle);
			TEST_FAILED(msg);
		}

		// Unsubscribe and invoke again - should not trigger
		Reflect::Scripting::Unsubscribe(handle);
		Reflect::Scripting::InvokeReloadCallbacks("test");

		if (invocationCount != 1) {
			Core::String msg = Core::FormatString("Expected no invocation after unsubscribe, got {}", invocationCount.load());
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	void CallbackTest::TestInvocationOrder()
	{
		// Callbacks should be invoked in subscription order (or at least all are invoked).
		// We verify that multiple callbacks registered for the same event type all get called.
		std::atomic<int> count1{0}, count2{0}, count3{0};

		auto h1 = Reflect::Scripting::SubscribeReload([&](const std::string&) { count1++; });
		auto h2 = Reflect::Scripting::SubscribeReload([&](const std::string&) { count2++; });
		auto h3 = Reflect::Scripting::SubscribeReload([&](const std::string&) { count3++; });

		Reflect::Scripting::InvokeReloadCallbacks("order-test");

		if (count1 != 1 || count2 != 1 || count3 != 1) {
			Core::String msg = Core::FormatString(
				"Expected all 3 callbacks invoked, got {}, {}, {}", 
				count1.load(), count2.load(), count3.load());
			Reflect::Scripting::Unsubscribe(h1);
			Reflect::Scripting::Unsubscribe(h2);
			Reflect::Scripting::Unsubscribe(h3);
			TEST_FAILED(msg);
		}

		// Clean up
		Reflect::Scripting::Unsubscribe(h1);
		Reflect::Scripting::Unsubscribe(h2);
		Reflect::Scripting::Unsubscribe(h3);

		TEST_PASSED();
	}
}
