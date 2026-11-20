#pragma once
#include "Json/JsonValue.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace C4LSP::Server
{
	// The messages a client sent that the server has not handled yet, read off the channel by a thread of their own as
	// they arrive (docs/tools/03 "Requests while it analyzes"): a request that waits for an analysis sees the
	// cancellations and the edits sent after it, and its budget counts from when it arrived.
	class MessageInbox
	{
	public:
		struct Arrived
		{
			Json::Value message;
			std::chrono::steady_clock::time_point at;
		};

		// The reader: a message as it arrives (a $/cancelRequest marks its request cancelled at once); then what
		// OnArrival gave runs, outside the inbox's lock.
		void Push(Json::Value message);
		// The reader: the channel ended; Next answers what is left, then nothing.
		void Close();
		// The next message in the order they came, waiting for one; nullopt once closed and empty.
		std::optional<Arrived> Next();
		// The same, but nullopt too once `until` passes or Poke is called first: the server then looks at its parked requests.
		std::optional<Arrived> NextBefore(std::chrono::steady_clock::time_point until);
		// Wakes a NextBefore waiting now, or makes the next one return at once (an analysis came in: a parked request may
		// answer now).
		void Poke();
		// Whether the channel ended and every message it brought has been taken.
		bool Drained() const;

		// Whether the client cancelled the request `id` (a $/cancelRequest for it arrived).
		bool IsCancelled(const Json::Value& id) const;
		// The request `id` is answered: its cancellation is forgotten (the server takes the $/cancelRequest after it).
		void Forget(const Json::Value& id);
		// Whether a didChange or didClose of `uri` waits here: a request about `uri` before it is for text gone.
		bool HasEditOf(const std::string& uri) const;

		// What runs after each message arrives (the server wakes the requests waiting for an analysis); an empty one
		// stops that, and returns once no call of the one before runs any more.
		void OnArrival(std::function<void()> wake);

	private:
		void Wake();

		mutable std::mutex m_Lock;
		std::condition_variable m_Arrived;
		std::deque<Arrived> m_Waiting;
		std::set<std::string> m_Cancelled; // the cancelled requests' ids, as IdKey writes them
		bool m_Closed = false;
		bool m_Poked = false;
		std::mutex m_WakeLock;             // held while m_Wake runs, so a server that stops listening is never called after
		std::function<void()> m_Wake;
	};

	// A request id as one key: an integer and a string id never meet.
	std::string IdKey(const Json::Value& id);
} // namespace C4LSP::Server
