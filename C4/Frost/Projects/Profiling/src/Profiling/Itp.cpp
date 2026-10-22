// Itp.cpp - the IED Trace Protocol client (IED docs/16 "ITP v1 wire format", "Client library requirements"). One
// session per process: each thread writes encoded events into a buffer of its own under an uncontended lock; a
// flusher thread takes every thread's buffer, then the global one (definitions, memory), and sends the global chunk
// first, so a definition always arrives before the event that names it. Events are stamped with the processor's time
// stamp counter, its frequency measured at start and corrected on the server by calibration pairs.

#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Itp.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
static const SocketHandle kNoSocket = INVALID_SOCKET;
#else
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
using SocketHandle = int;
static const SocketHandle kNoSocket = -1;
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#define ITP_HAS_TSC 1
#endif

namespace
{
	enum class Event : uint8_t
	{
		Hello = 0x01, Calibration = 0x02, Bye = 0x03,
		StringDef = 0x10, SrcLocDef = 0x11, ThreadDef = 0x12, ThreadName = 0x13,
		ZoneBegin = 0x20, ZoneBeginNamed = 0x21, ZoneEnd = 0x22, ZoneText = 0x23, ZoneValue = 0x24,
		TaskBegin = 0x30, TaskEnd = 0x31, TaskSuspend = 0x32, TaskResume = 0x33,
		FrameMark = 0x40, FrameBegin = 0x41, FrameEnd = 0x42,
		PlotDef = 0x50, PlotValue = 0x51, Message = 0x52,
		PoolDef = 0x60, Alloc = 0x61, Free = 0x62, PoolReset = 0x63,
		LockDef = 0x70, LockWait = 0x71, LockObtain = 0x72, LockRelease = 0x73,
	};

	constexpr uint8_t kThreadChunk = 0;
	constexpr uint8_t kGlobalChunk = 1;
	constexpr uint16_t kProtocolVersion = 1;
	constexpr uint64_t kEstimatedClock = 1;
	constexpr auto kFlushEvery = std::chrono::milliseconds(10);
	constexpr auto kFrequencyProbe = std::chrono::milliseconds(20);
	constexpr int64_t kCalibrateEverySeconds = 1;

	enum State : int { kUnknown, kOff, kOn };

	// --- Encoding ---------------------------------------------------------------------------------------------------

	using Bytes = std::vector<uint8_t>;

	void PutVarint(Bytes& out, uint64_t value)
	{
		while (value >= 0x80)
		{
			out.push_back((uint8_t)((value & 0x7F) | 0x80));
			value >>= 7;
		}
		out.push_back((uint8_t)value);
	}

	void PutZigzag(Bytes& out, int64_t value)
	{
		PutVarint(out, value >= 0 ? (uint64_t)value << 1 : (((uint64_t)(-(value + 1))) << 1) | 1);
	}

	void PutText(Bytes& out, const char* text, size_t length)
	{
		PutVarint(out, length);
		out.insert(out.end(), (const uint8_t*)text, (const uint8_t*)text + length);
	}

	void PutDouble(Bytes& out, double value)
	{
		uint64_t bits;
		std::memcpy(&bits, &value, sizeof bits);
		for (int i = 0; i < 8; ++i) out.push_back((uint8_t)(bits >> (8 * i)));
	}

	void PutEvent(Bytes& out, Event type) { out.push_back((uint8_t)type); }

	void PutChunk(Bytes& out, uint8_t kind, uint64_t thread, int64_t base, const Bytes& body)
	{
		Bytes head;
		head.push_back(kind);
		PutVarint(head, thread);
		PutVarint(head, (uint64_t)base);
		const uint32_t length = (uint32_t)(head.size() + body.size());
		for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(length >> (8 * i)));
		out.insert(out.end(), head.begin(), head.end());
		out.insert(out.end(), body.begin(), body.end());
	}

	// --- The clock --------------------------------------------------------------------------------------------------

	int64_t Now()
	{
#ifdef ITP_HAS_TSC
		return (int64_t)__rdtsc();
#else
		return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
	}

	int64_t UnixNs()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	// Ticks a second of Now(), measured against the steady clock.
	int64_t MeasureFrequency()
	{
#ifdef ITP_HAS_TSC
		const auto wall0 = std::chrono::steady_clock::now();
		const int64_t ticks0 = Now();
		std::this_thread::sleep_for(kFrequencyProbe);
		const int64_t ticks1 = Now();
		const auto wall1 = std::chrono::steady_clock::now();
		const double seconds = std::chrono::duration<double>(wall1 - wall0).count();
		return seconds > 0.0 ? (int64_t)((double)(ticks1 - ticks0) / seconds) : 1000000000;
#else
		return 1000000000;
#endif
	}

	uint64_t ProcessId()
	{
#if defined(_WIN32)
		return GetCurrentProcessId();
#else
		return (uint64_t)getpid();
#endif
	}

	uint64_t OsThreadId()
	{
#if defined(_WIN32)
		return GetCurrentThreadId();
#else
		return (uint64_t)std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
	}

	// --- Buffers and the session ------------------------------------------------------------------------------------

	// Events since the flusher last took them; `base` is the time the first delta of these bytes is from.
	struct Buffer
	{
		std::mutex lock;
		Bytes bytes;
		int64_t base = 0;
		int64_t last = 0;
		uint64_t thread = 0;
		std::atomic<bool> ended{ false };

		void Timed(Event type, int64_t ticks)
		{
			PutEvent(bytes, type);
			PutZigzag(bytes, ticks - last);
			last = ticks;
		}
	};

	struct Session
	{
		int64_t number = 0;
		int64_t frequency = 0;
		int64_t startTicks = 0;
		SocketHandle socket = kNoSocket;
		FILE* file = nullptr;
		std::mutex lock;                       // the maps, counters and thread list
		std::unordered_map<std::string, uint64_t> strings;
		std::unordered_map<std::string, uint64_t> namedSites;
		std::unordered_map<std::string, uint64_t> plots;
		uint64_t nextString = 0;
		uint64_t nextSite = 0;
		uint64_t nextPlot = 0;
		uint64_t nextThread = 0;
		uint64_t nextLock = 0;
		std::atomic<uint64_t> nextTask{ 0 };
		std::vector<std::shared_ptr<Buffer>> threads;
		Buffer global;
		std::atomic<bool> stopping{ false };
		std::thread flusher;
		int64_t lastCalibration = 0;

		// The id of `text`, its StringDef queued the first time; the lock is held.
		uint64_t Intern(const char* text, size_t length)
		{
			if (length == 0) return 0;
			std::string key(text, length);
			auto found = strings.find(key);
			if (found != strings.end()) return found->second;
			const uint64_t id = Once(text, length);
			strings.emplace(std::move(key), id);
			return id;
		}

		// A fresh id for text sent once (a message, a zone's text); the lock is held.
		uint64_t Once(const char* text, size_t length)
		{
			nextString += 1;
			std::lock_guard<std::mutex> held(global.lock);
			PutEvent(global.bytes, Event::StringDef);
			PutVarint(global.bytes, nextString);
			PutText(global.bytes, text, length);
			return nextString;
		}

		bool Send(const Bytes& bytes)
		{
			if (bytes.empty()) return true;
			if (file) return std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
			size_t sent = 0;
			while (sent < bytes.size())
			{
				const int n = ::send(socket, (const char*)bytes.data() + sent, (int)(bytes.size() - sent), 0);
				if (n <= 0) return false;
				sent += (size_t)n;
			}
			return true;
		}
	};

	std::atomic<int> g_state{ kUnknown };
	std::mutex g_startLock;
	Session* g_session = nullptr;
	int64_t g_sessions = 0;

	struct ThreadSlot
	{
		std::shared_ptr<Buffer> buffer;
		int64_t session = 0;
		~ThreadSlot() { if (buffer) buffer->ended.store(true, std::memory_order_release); }
	};

	thread_local ThreadSlot t_slot;

	// Set while the calling thread runs the client's own code (and for the flusher's whole life): an allocation the
	// client makes there is not reported, so a program whose operator new reports to itp_alloc neither recurses nor
	// deadlocks on the client's locks.
	thread_local int t_inside = 0;

	struct Inside
	{
		Inside() { ++t_inside; }
		~Inside() { --t_inside; }
	};

	Session* Running();

	Buffer* ThreadBuffer(Session* session)
	{
		if (t_slot.buffer && t_slot.session == session->number) return t_slot.buffer.get();
		auto buffer = std::make_shared<Buffer>();
		buffer->last = session->startTicks;
		buffer->base = session->startTicks;
		{
			std::lock_guard<std::mutex> held(session->lock);
			buffer->thread = ++session->nextThread;
			session->threads.push_back(buffer);
			std::lock_guard<std::mutex> global(session->global.lock);
			PutEvent(session->global.bytes, Event::ThreadDef);
			PutVarint(session->global.bytes, buffer->thread);
			PutVarint(session->global.bytes, OsThreadId());
			PutVarint(session->global.bytes, 0);
		}
		t_slot.buffer = buffer;
		t_slot.session = session->number;
		return buffer.get();
	}

	// Takes a buffer's events; false when it had none.
	bool Take(Buffer& buffer, Bytes& bytes, int64_t& base)
	{
		std::lock_guard<std::mutex> held(buffer.lock);
		if (buffer.bytes.empty()) return false;
		buffer.bytes.swap(bytes);
		base = buffer.base;
		buffer.base = buffer.last;
		return true;
	}

	void QueueCalibration(Session* session, int64_t now)
	{
		std::lock_guard<std::mutex> held(session->global.lock);
		PutEvent(session->global.bytes, Event::Calibration);
		PutVarint(session->global.bytes, (uint64_t)now);
		PutVarint(session->global.bytes, (uint64_t)UnixNs());
	}

	bool FlushOnce(Session* session)
	{
		const int64_t now = Now();
		if (now - session->lastCalibration >= session->frequency * kCalibrateEverySeconds)
		{
			session->lastCalibration = now;
			QueueCalibration(session, now);
		}
		std::vector<std::shared_ptr<Buffer>> buffers;
		{
			std::lock_guard<std::mutex> held(session->lock);
			buffers = session->threads;
		}
		Bytes threads;
		for (auto& buffer : buffers)
		{
			Bytes body;
			int64_t base;
			if (Take(*buffer, body, base)) PutChunk(threads, kThreadChunk, buffer->thread, base, body);
		}
		Bytes out;
		Bytes globalBody;
		int64_t globalBase;
		if (Take(session->global, globalBody, globalBase)) PutChunk(out, kGlobalChunk, 0, globalBase, globalBody);
		out.insert(out.end(), threads.begin(), threads.end());
		const bool sent = session->Send(out);
		std::lock_guard<std::mutex> held(session->lock);
		auto& list = session->threads;
		for (size_t i = list.size(); i-- > 0;)
		{
			std::lock_guard<std::mutex> buffer(list[i]->lock);
			if (list[i]->ended.load(std::memory_order_acquire) && list[i]->bytes.empty()) list.erase(list.begin() + (ptrdiff_t)i);
		}
		return sent;
	}

	void FlushLoop(Session* session)
	{
		Inside flushing;
		while (!session->stopping.load(std::memory_order_acquire))
		{
			std::this_thread::sleep_for(kFlushEvery);
			if (!FlushOnce(session))
			{
				g_state.store(kOff, std::memory_order_release);
				return;
			}
		}
	}

	void SetProblem(char* problem, size_t size, const std::string& text)
	{
		if (!problem || size == 0) return;
		std::snprintf(problem, size, "%s", text.c_str());
	}

	bool Connect(Session* session, const std::string& address, std::string& problem)
	{
		const size_t colon = address.rfind(':');
		if (colon == std::string::npos)
		{
			problem = "not an address: " + address;
			return false;
		}
		std::string host = address.substr(0, colon);
		const std::string port = address.substr(colon + 1);
		if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
#if defined(_WIN32)
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* found = nullptr;
		if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0 || !found)
		{
			problem = "cannot resolve " + address;
			return false;
		}
		for (addrinfo* at = found; at; at = at->ai_next)
		{
			SocketHandle s = ::socket(at->ai_family, at->ai_socktype, at->ai_protocol);
			if (s == kNoSocket) continue;
			if (::connect(s, at->ai_addr, (int)at->ai_addrlen) == 0)
			{
				int on = 1;
				setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof on);
				session->socket = s;
				break;
			}
#if defined(_WIN32)
			closesocket(s);
#else
			close(s);
#endif
		}
		freeaddrinfo(found);
		if (session->socket == kNoSocket) problem = "cannot reach the profiler at " + address;
		return session->socket != kNoSocket;
	}

	bool Open(Session* session, const std::string& target, std::string& token, std::string& problem)
	{
		if (target.rfind("file:", 0) == 0)
		{
			session->file = std::fopen(target.c_str() + 5, "wb");
			if (!session->file) problem = "cannot write " + target.substr(5);
			return session->file != nullptr;
		}
		const size_t at = target.find(";token=");
		if (at != std::string::npos) token = target.substr(at + 7);
		return Connect(session, target.substr(0, at), problem);
	}

	void Close(Session* session)
	{
		if (session->file) std::fclose(session->file);
		session->file = nullptr;
		if (session->socket == kNoSocket) return;
#if defined(_WIN32)
		shutdown(session->socket, SD_BOTH);
		closesocket(session->socket);
#else
		shutdown(session->socket, SHUT_RDWR);
		close(session->socket);
#endif
		session->socket = kNoSocket;
	}

	Bytes HelloStream(Session* session, const std::string& app, const std::string& token)
	{
		Bytes body;
		PutEvent(body, Event::Hello);
		PutVarint(body, kProtocolVersion);
		PutText(body, app.data(), app.size());
		PutVarint(body, ProcessId());
		PutVarint(body, (uint64_t)session->frequency);
		PutVarint(body, (uint64_t)session->startTicks);
		PutVarint(body, (uint64_t)UnixNs());
#ifdef ITP_HAS_TSC
		PutVarint(body, kEstimatedClock);
#else
		PutVarint(body, 0);
#endif
		PutText(body, token.data(), token.size());
		Bytes out = { 'I', 'T', 'P', '1', (uint8_t)(kProtocolVersion & 0xFF), (uint8_t)(kProtocolVersion >> 8), 0, 0 };
		PutChunk(out, kGlobalChunk, 0, session->startTicks, body);
		return out;
	}

	void StopAtExit() { itp_stop(); }

	bool StartSession(const std::string& target, const std::string& app, std::string& problem)
	{
		std::lock_guard<std::mutex> starting(g_startLock);
		if (g_state.load(std::memory_order_acquire) == kOn)
		{
			problem = "a profiling session is running";
			return false;
		}
		auto session = std::make_unique<Session>();
		std::string token;
		if (!Open(session.get(), target, token, problem)) return false;
		session->number = ++g_sessions;
		session->frequency = MeasureFrequency();
		session->startTicks = Now();
		session->global.last = session->global.base = session->startTicks;
		session->lastCalibration = session->startTicks;
		if (!session->Send(HelloStream(session.get(), app, token)))
		{
			problem = "the profiler closed the connection";
			Close(session.get());
			return false;
		}
		QueueCalibration(session.get(), session->startTicks);
		Session* started = session.release();
		started->flusher = std::thread(FlushLoop, started);
		g_session = started;
		static bool registered = false;
		if (!registered)
		{
			registered = true;
			std::atexit(StopAtExit);
		}
		g_state.store(kOn, std::memory_order_release);
		return true;
	}

	std::string ProgramName()
	{
#if defined(_WIN32)
		char path[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		std::string name = path;
#else
		std::string name = "program";
#endif
		const size_t slash = name.find_last_of("/\\");
		if (slash != std::string::npos) name = name.substr(slash + 1);
		const size_t dot = name.rfind('.');
		return dot == std::string::npos ? name : name.substr(0, dot);
	}

	Session* Running()
	{
		int state = g_state.load(std::memory_order_acquire);
		if (state == kOn) return g_session;
		if (state != kUnknown) return nullptr;
		if (!g_state.compare_exchange_strong(state, kOff, std::memory_order_acq_rel)) return g_state.load() == kOn ? g_session : nullptr;
		const char* target = std::getenv("IED_PROFILER");
		std::string problem;
		if (target && *target) StartSession(target, ProgramName(), problem);
		return g_state.load(std::memory_order_acquire) == kOn ? g_session : nullptr;
	}

	// --- Events -----------------------------------------------------------------------------------------------------

	void ThreadEvent(Session* session, Event type, uint64_t field)
	{
		Buffer* buffer = ThreadBuffer(session);
		const int64_t now = Now();
		std::lock_guard<std::mutex> held(buffer->lock);
		buffer->Timed(type, now);
		PutVarint(buffer->bytes, field);
	}

	uint64_t Interned(Session* session, const char* text)
	{
		if (!text) return 0;
		std::lock_guard<std::mutex> held(session->lock);
		return session->Intern(text, std::strlen(text));
	}

	uint64_t SiteId(Session* session, itp_site* site)
	{
		std::atomic_ref<int64_t> owner(site->session);
		if (owner.load(std::memory_order_acquire) == session->number) return site->id;
		std::lock_guard<std::mutex> held(session->lock);
		if (site->session == session->number) return site->id;
		const uint64_t name = site->name ? session->Intern(site->name, std::strlen(site->name)) : 0;
		const uint64_t function = site->function ? session->Intern(site->function, std::strlen(site->function)) : 0;
		const uint64_t file = site->file ? session->Intern(site->file, std::strlen(site->file)) : 0;
		const uint64_t id = ++session->nextSite;
		{
			std::lock_guard<std::mutex> global(session->global.lock);
			PutEvent(session->global.bytes, Event::SrcLocDef);
			PutVarint(session->global.bytes, id);
			PutVarint(session->global.bytes, name);
			PutVarint(session->global.bytes, function);
			PutVarint(session->global.bytes, file);
			PutVarint(session->global.bytes, site->line);
			PutVarint(session->global.bytes, site->color);
		}
		site->id = id;
		owner.store(session->number, std::memory_order_release);
		return id;
	}

	// The lock's id in this session, its place and its LockDef queued the first time.
	uint64_t LockId(Session* session, itp_lock* lock)
	{
		std::atomic_ref<int64_t> owner(lock->session);
		if (owner.load(std::memory_order_acquire) == session->number) return lock->id;
		std::lock_guard<std::mutex> held(session->lock);
		if (lock->session == session->number) return lock->id;
		const uint64_t name = lock->name ? session->Intern(lock->name, std::strlen(lock->name)) : 0;
		const uint64_t file = lock->file ? session->Intern(lock->file, std::strlen(lock->file)) : 0;
		const uint64_t site = ++session->nextSite;
		const uint64_t id = ++session->nextLock;
		{
			std::lock_guard<std::mutex> global(session->global.lock);
			PutEvent(session->global.bytes, Event::SrcLocDef);
			for (uint64_t field : { site, name, (uint64_t)0, file, (uint64_t)lock->line, (uint64_t)0 }) PutVarint(session->global.bytes, field);
			PutEvent(session->global.bytes, Event::LockDef);
			PutVarint(session->global.bytes, id);
			PutVarint(session->global.bytes, name);
			PutVarint(session->global.bytes, site);
		}
		lock->id = id;
		owner.store(session->number, std::memory_order_release);
		return id;
	}

	void LockEvent(Event type, itp_lock* lock, int shared)
	{
		Session* session = Running();
		if (session && lock) ThreadEvent(session, type, LockId(session, lock) * 2 + (shared ? 1 : 0));
	}

	void MemoryEvent(Session* session, Event type, uint32_t pool, const void* address, size_t size)
	{
		const uint64_t thread = ThreadBuffer(session)->thread;
		const int64_t now = Now();
		std::lock_guard<std::mutex> held(session->global.lock);
		session->global.Timed(type, now);
		PutVarint(session->global.bytes, thread);
		PutVarint(session->global.bytes, pool);
		PutVarint(session->global.bytes, (uint64_t)(uintptr_t)address);
		if (type == Event::Alloc)
		{
			PutVarint(session->global.bytes, size);
			PutVarint(session->global.bytes, 0);
		}
	}
}

// --- The C API ----------------------------------------------------------------------------------------------------

extern "C" int itp_start(const char* target, const char* app, char* problem, size_t problem_size)
{
	Inside inside;
	Running();
	std::string why;
	if (!target || !StartSession(target, app ? app : ProgramName(), why))
	{
		SetProblem(problem, problem_size, target ? why : "no target");
		return -1;
	}
	return 0;
}

extern "C" void itp_stop(void)
{
	Inside inside;
	std::lock_guard<std::mutex> stopping(g_startLock);
	int on = kOn;
	if (!g_state.compare_exchange_strong(on, kOff, std::memory_order_acq_rel)) return;
	Session* session = g_session;
	session->stopping.store(true, std::memory_order_release);
	if (session->flusher.joinable()) session->flusher.join();
	FlushOnce(session);
	Bytes body;
	PutEvent(body, Event::Bye);
	PutVarint(body, 0);
	Bytes bye;
	PutChunk(bye, kGlobalChunk, 0, session->startTicks, body);
	session->Send(bye);
	Close(session);
}

extern "C" int itp_active(void)
{
	Inside inside;
	return Running() != nullptr;
}

extern "C" void itp_zone_begin(itp_site* site)
{
	Inside inside;
	Session* session = Running();
	if (session) ThreadEvent(session, Event::ZoneBegin, SiteId(session, site));
}

extern "C" void itp_zone_begin_named(itp_site* site, const char* name, size_t length)
{
	Inside inside;
	Session* session = Running();
	if (!session) return;
	const uint64_t id = SiteId(session, site);
	uint64_t text;
	{
		std::lock_guard<std::mutex> held(session->lock);
		text = session->Intern(name, length);
	}
	Buffer* buffer = ThreadBuffer(session);
	const int64_t now = Now();
	std::lock_guard<std::mutex> held(buffer->lock);
	buffer->Timed(Event::ZoneBeginNamed, now);
	PutVarint(buffer->bytes, id);
	PutVarint(buffer->bytes, text);
}

extern "C" void itp_zone_end(void)
{
	Inside inside;
	Session* session = Running();
	if (!session) return;
	Buffer* buffer = ThreadBuffer(session);
	const int64_t now = Now();
	std::lock_guard<std::mutex> held(buffer->lock);
	buffer->Timed(Event::ZoneEnd, now);
}

extern "C" void itp_zone_text(const char* text, size_t length)
{
	Inside inside;
	Session* session = Running();
	if (!session) return;
	uint64_t id;
	{
		std::lock_guard<std::mutex> held(session->lock);
		id = session->Once(text, length);
	}
	Buffer* buffer = ThreadBuffer(session);
	std::lock_guard<std::mutex> held(buffer->lock);
	PutEvent(buffer->bytes, Event::ZoneText);
	PutVarint(buffer->bytes, id);
}

extern "C" void itp_zone_value(uint64_t value)
{
	Inside inside;
	Session* session = Running();
	if (!session) return;
	Buffer* buffer = ThreadBuffer(session);
	std::lock_guard<std::mutex> held(buffer->lock);
	PutEvent(buffer->bytes, Event::ZoneValue);
	PutVarint(buffer->bytes, value);
}

extern "C" void itp_frame(const char* set)
{
	Inside inside;
	Session* session = Running();
	if (session) ThreadEvent(session, Event::FrameMark, Interned(session, set));
}

extern "C" void itp_frame_begin(const char* set)
{
	Inside inside;
	Session* session = Running();
	if (session) ThreadEvent(session, Event::FrameBegin, Interned(session, set));
}

extern "C" void itp_frame_end(const char* set)
{
	Inside inside;
	Session* session = Running();
	if (session) ThreadEvent(session, Event::FrameEnd, Interned(session, set));
}

extern "C" void itp_plot(const char* name, double value)
{
	Inside inside;
	Session* session = Running();
	if (!session || !name) return;
	uint64_t id;
	{
		std::lock_guard<std::mutex> held(session->lock);
		auto found = session->plots.find(name);
		if (found != session->plots.end()) id = found->second;
		else
		{
			const uint64_t text = session->Intern(name, std::strlen(name));
			id = ++session->nextPlot;
			session->plots.emplace(name, id);
			std::lock_guard<std::mutex> global(session->global.lock);
			PutEvent(session->global.bytes, Event::PlotDef);
			PutVarint(session->global.bytes, id);
			PutVarint(session->global.bytes, text);
			PutVarint(session->global.bytes, 0);
			PutVarint(session->global.bytes, 0);
		}
	}
	Buffer* buffer = ThreadBuffer(session);
	const int64_t now = Now();
	std::lock_guard<std::mutex> held(buffer->lock);
	buffer->Timed(Event::PlotValue, now);
	PutVarint(buffer->bytes, id);
	PutDouble(buffer->bytes, value);
}

extern "C" void itp_message(int severity, const char* text, size_t length)
{
	Inside inside;
	Session* session = Running();
	if (!session || !text) return;
	uint64_t id;
	{
		std::lock_guard<std::mutex> held(session->lock);
		id = session->Once(text, length);
	}
	Buffer* buffer = ThreadBuffer(session);
	const int64_t now = Now();
	std::lock_guard<std::mutex> held(buffer->lock);
	buffer->Timed(Event::Message, now);
	PutVarint(buffer->bytes, (uint64_t)(severity < 0 ? 0 : severity));
	PutVarint(buffer->bytes, id);
	PutVarint(buffer->bytes, 0);
}

extern "C" void itp_thread_name(const char* name)
{
	Inside inside;
	Session* session = Running();
	if (session && name) ThreadEvent(session, Event::ThreadName, Interned(session, name));
}

extern "C" uint64_t itp_task_begin(const char* name, uint64_t parent)
{
	Inside inside;
	Session* session = Running();
	if (!session) return 0;
	const uint64_t text = Interned(session, name);
	const uint64_t id = session->nextTask.fetch_add(1, std::memory_order_relaxed) + 1;
	Buffer* buffer = ThreadBuffer(session);
	const int64_t now = Now();
	std::lock_guard<std::mutex> held(buffer->lock);
	buffer->Timed(Event::TaskBegin, now);
	PutVarint(buffer->bytes, id);
	PutVarint(buffer->bytes, text);
	PutVarint(buffer->bytes, parent);
	return id;
}

extern "C" void itp_task_end(uint64_t task)
{
	Inside inside;
	Session* session = Running();
	if (session && task) ThreadEvent(session, Event::TaskEnd, task);
}

extern "C" void itp_task_suspend(uint64_t task)
{
	Inside inside;
	Session* session = Running();
	if (session && task) ThreadEvent(session, Event::TaskSuspend, task);
}

extern "C" void itp_task_resume(uint64_t task)
{
	Inside inside;
	Session* session = Running();
	if (session && task) ThreadEvent(session, Event::TaskResume, task);
}

extern "C" void itp_alloc(const void* address, size_t size, uint32_t pool)
{
	if (t_inside) return;
	Inside inside;
	Session* session = Running();
	if (session) MemoryEvent(session, Event::Alloc, pool, address, size);
}

extern "C" void itp_free(const void* address, uint32_t pool)
{
	if (t_inside) return;
	Inside inside;
	Session* session = Running();
	if (session) MemoryEvent(session, Event::Free, pool, address, 0);
}

extern "C" void itp_pool_name(uint32_t pool, const char* name)
{
	Inside inside;
	Session* session = Running();
	if (!session || !name) return;
	const uint64_t text = Interned(session, name);
	std::lock_guard<std::mutex> held(session->global.lock);
	PutEvent(session->global.bytes, Event::PoolDef);
	PutVarint(session->global.bytes, pool);
	PutVarint(session->global.bytes, text);
}

extern "C" void itp_lock_wait(itp_lock* lock, int shared)
{
	Inside inside;
	LockEvent(Event::LockWait, lock, shared);
}

extern "C" void itp_lock_obtained(itp_lock* lock, int shared)
{
	Inside inside;
	LockEvent(Event::LockObtain, lock, shared);
}

extern "C" void itp_lock_released(itp_lock* lock, int shared)
{
	Inside inside;
	LockEvent(Event::LockRelease, lock, shared);
}

extern "C" void itp_pool_reset(uint32_t pool)
{
	Inside inside;
	Session* session = Running();
	if (!session) return;
	const int64_t now = Now();
	std::lock_guard<std::mutex> held(session->global.lock);
	session->global.Timed(Event::PoolReset, now);
	PutVarint(session->global.bytes, pool);
}
