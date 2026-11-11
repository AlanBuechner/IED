#include "CompilerMessageQueueTest.h"
#include "Compiler/CompilerMessageQueue.h"
#include <atomic>
#include <chrono>
#include <thread>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// Long enough for a waiter that would return at once to have done so.
		constexpr auto kSettle = std::chrono::milliseconds(100);

		// A parser thread asking for an include, as IncludeDeclarationNode does.
		class Waiter
		{
		public:
			Waiter(CompilerMessageQueue& queue, uint32 sourceId)
				: m_Thread([this, &queue, sourceId] {
					m_Response = queue.SendMessageAndAwaitResponse(sourceId, MessageType::RequestFileInclude, "a.ch");
					m_Done = true;
				})
			{
			}
			~Waiter() { if (m_Thread.joinable()) m_Thread.join(); }

			bool Done() const { return m_Done; }
			const CompilerResponse& Response() { m_Thread.join(); return m_Response; }

		private:
			std::atomic<bool> m_Done{ false };
			CompilerResponse m_Response{};
			std::thread m_Thread;
		};
	}

	void CompilerMessageQueueTest::AWaiterTakesAReleasedResponse()
	{
		CompilerMessageQueue queue;
		Waiter waiter(queue, 3);
		const CompilerMessage request = queue.WaitPopRequest();
		TEST_ASSERT(request.sourceId == 3 && request.type == MessageType::RequestFileInclude && request.data == "a.ch");
		queue.PushResponse(3, ResponseType::Error, "include not found");
		TEST_ASSERT(queue.FlushPendingResponse(3));
		const CompilerResponse& response = waiter.Response();
		TEST_ASSERT(response.type == ResponseType::Error && response.data == "include not found");
	}

	void CompilerMessageQueueTest::APushedResponseIsNotTakenBeforeItIsReleased()
	{
		CompilerMessageQueue queue;
		queue.PushResponse(5, ResponseType::Success, "");
		Waiter waiter(queue, 5);
		queue.WaitPopRequest();
		std::this_thread::sleep_for(kSettle);
		TEST_ASSERT(!waiter.Done());
		TEST_ASSERT(queue.FlushPendingResponse(5));
		TEST_ASSERT(waiter.Response().type == ResponseType::Success);
	}

	void CompilerMessageQueueTest::ReleasingWithNothingPushedDoesNothing()
	{
		CompilerMessageQueue queue;
		TEST_ASSERT(!queue.FlushPendingResponse(9));
		queue.PushResponse(9, ResponseType::Success, "");
		TEST_ASSERT(queue.FlushPendingResponse(9));
		TEST_ASSERT(!queue.FlushPendingResponse(9));
	}

	void CompilerMessageQueueTest::AResponseGoesOnlyToItsOwnSource()
	{
		CompilerMessageQueue queue;
		Waiter first(queue, 1);
		Waiter second(queue, 2);
		queue.WaitPopRequest();
		queue.WaitPopRequest();
		queue.PushResponse(2, ResponseType::Success, "two");
		TEST_ASSERT(queue.FlushPendingResponse(2));
		TEST_ASSERT(second.Response().data == "two");
		std::this_thread::sleep_for(kSettle);
		TEST_ASSERT(!first.Done());
		queue.PushResponse(1, ResponseType::Success, "one");
		TEST_ASSERT(queue.FlushPendingResponse(1));
		TEST_ASSERT(first.Response().data == "one");
	}
} // namespace Testing
