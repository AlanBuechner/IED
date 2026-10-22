// A program over the C/C++ ITP client (plugins/cpp/libraries/Profiling/src/Profiling/Itp.h) for test_itp_client.py:
// zones on two threads, a named zone with text and a value, frames, a plot, a message, tasks, a pool, and every
// operator new reported the way the C++ Core library's memory tracker does. argv[1] is the target; without it the client starts
// from IED_PROFILER and the session ends at exit.
#include "Profiling/Itp.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

void* operator new(size_t size)
{
	void* ptr = std::malloc(size ? size : 1);
	if (!ptr)
		throw std::bad_alloc();
	itp_alloc(ptr, size, 0);
	return ptr;
}

void operator delete(void* ptr) noexcept
{
	if (!ptr)
		return;
	itp_free(ptr, 0);
	std::free(ptr);
}

void operator delete(void* ptr, size_t) noexcept
{
	operator delete(ptr);
}

static ITP_LOCKABLE(std::mutex, queue, "Queue");

// The main thread holds the queue while a thread waits for it, then takes it again without waiting.
static void Contend()
{
	std::atomic<bool> waiting{ false };
	queue.lock();
	std::thread waiter([&] {
		waiting = true;
		std::lock_guard<decltype(queue)> held(queue);
	});
	while (!waiting) std::this_thread::yield();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	queue.unlock();
	waiter.join();
	std::lock_guard<decltype(queue)> again(queue);
}

static int Spin(int count)
{
	ITP_ZONE("Spin");
	volatile int sum = 0;
	for (int i = 0; i < count; ++i)
		sum += i;
	return sum;
}

static void Frame(int frame)
{
	ITP_ZONE("Update");
	{
		static itp_site loadSite = { "Load", __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0, 0, 0 };
		itp::Zone load(&loadSite, "level.bin");
		load.Text("bytes=4096");
		load.Value(4096);
		std::vector<std::string> names(16, std::string(64, 'x'));
		Spin(10000);
	}
	std::thread worker([] {
		ITP_THREAD_NAME("Worker");
		itp::Task task("Physics");
		ITP_ZONE("Physics::Step");
		Spin(20000);
	});
	worker.join();
	ITP_PLOT("Frame", frame);
}

int main(int argc, char** argv)
{
	if (argc > 1)
	{
		char problem[256] = {};
		if (itp_start(argv[1], "itp-client-test", problem, sizeof problem) != 0)
		{
			std::printf("start failed: %s\n", problem);
			return 2;
		}
	}
	ITP_THREAD_NAME("Main");
	itp_frame_begin("Loading");
	Spin(1000);
	itp_frame_end("Loading");
	for (int frame = 0; frame < 5; ++frame)
	{
		Frame(frame);
		ITP_FRAME();
	}
	Contend();
	itp_message(ITP_WARNING, "done", 4);
	int block = 0;
	itp_pool_name(2, "Arena");
	itp_alloc(&block, 64, 2);
	itp_free(&block, 2);
	if (argc > 1)
		itp_stop();
	return 0;
}
