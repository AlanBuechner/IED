#include "control.h"
#include "capture.h"
#include "json.h"

#include <windows.h>

namespace fb::capture
{
	// The pipe is overlapped: a synchronous handle would hold every write until the pending read returned.
	static bool WaitIo(HANDLE pipe, OVERLAPPED& io, BOOL started, DWORD& bytes) {
		if (!started && GetLastError() != ERROR_IO_PENDING) return false;
		return GetOverlappedResult(pipe, &io, &bytes, TRUE) != 0;
	}

	ControlPipe::~ControlPipe() {
		stop_ = true;
		if (thread_.joinable()) thread_.detach();
	}

	void ControlPipe::Start() {
		thread_ = std::thread([this] { Serve(); });
	}

	void ControlPipe::Send(const std::string& line) {
		std::lock_guard<std::mutex> guard(lock_);
		if (!pipe_) return;
		std::string framed = line + "\n";
		OVERLAPPED io = {};
		io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		DWORD wrote = 0;
		const BOOL started = WriteFile((HANDLE)pipe_, framed.data(), (DWORD)framed.size(), nullptr, &io);
		WaitIo((HANDLE)pipe_, io, started, wrote);
		CloseHandle(io.hEvent);
	}

	void ControlPipe::Handle(const std::string& line) {
		JsonValue message;
		std::string error;
		if (!JsonValue::Parse(line.data(), line.size(), message, error)) return;
		const std::string cmd = message["cmd"].Str();
		if (cmd == "capture") session_.Trigger((int)message["frames"].Int(1));
		else if (cmd == "status") Send(session_.HelloJson());
		else if (cmd == "setCaptureDir") session_.SetCaptureDir(message["dir"].Str());
		else if (cmd == "comment") session_.SetComment(message["text"].Str());
	}

	void ControlPipe::Serve() {
		const std::wstring name = L"\\\\.\\pipe\\flashbang-" + std::to_wstring(GetCurrentProcessId());
		HANDLE readDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		while (!stop_) {
			// Only this user's processes may connect (the default security of a pipe this process makes).
			HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024, 64 * 1024, 0, nullptr);
			if (pipe == INVALID_HANDLE_VALUE) {
				Log("control pipe unavailable (error %lu)", GetLastError());
				break;
			}
			OVERLAPPED connect = {};
			connect.hEvent = readDone;
			ResetEvent(readDone);
			DWORD ignored = 0;
			const BOOL started = ConnectNamedPipe(pipe, &connect);
			const bool connected = GetLastError() == ERROR_PIPE_CONNECTED || WaitIo(pipe, connect, started, ignored);
			if (!connected) {
				CloseHandle(pipe);
				continue;
			}
			{
				std::lock_guard<std::mutex> guard(lock_);
				pipe_ = pipe;
			}
			Send(session_.HelloJson());
			std::string pending;
			char buffer[4096];
			for (;;) {
				OVERLAPPED io = {};
				io.hEvent = readDone;
				ResetEvent(readDone);
				DWORD read = 0;
				const BOOL began = ReadFile(pipe, buffer, sizeof(buffer), nullptr, &io);
				if (!WaitIo(pipe, io, began, read) || read == 0) break;
				pending.append(buffer, read);
				size_t newline;
				while ((newline = pending.find('\n')) != std::string::npos) {
					std::string line = pending.substr(0, newline);
					pending.erase(0, newline + 1);
					if (!line.empty() && line.back() == '\r') line.pop_back();
					if (!line.empty()) Handle(line);
				}
				if (pending.size() > 1024 * 1024) pending.clear();
			}
			{
				std::lock_guard<std::mutex> guard(lock_);
				pipe_ = nullptr;
			}
			DisconnectNamedPipe(pipe);
			CloseHandle(pipe);
		}
		CloseHandle(readDone);
	}
}
