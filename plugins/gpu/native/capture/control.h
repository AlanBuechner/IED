// control.h - the control pipe between IED and the program (docs/22 "Control pipe"): \\.\pipe\flashbang-<pid>, one
// JSON object per line each way. IED connects to ask for captures; the library answers with hello, status and
// captured events. A thread of its own serves one client at a time and serves the next when it leaves.
#pragma once

#include "fb_base.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace fb::capture
{
	class Session;

	class ControlPipe
	{
	public:
		explicit ControlPipe(Session& session) : session_(session) {}
		~ControlPipe();

		void Start();
		// Queues a line for the connected client (dropped when none is connected).
		void Send(const std::string& line);

	private:
		void Serve();
		void Handle(const std::string& line);

		Session& session_;
		std::thread thread_;
		std::mutex lock_;
		void* pipe_ = nullptr;      // the connected client's pipe, or null
		std::atomic<bool> stop_{false};
	};
}
