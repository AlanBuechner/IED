#pragma once
#include "Core/Core.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <utility>
#include <unordered_map>

namespace Scripting
{

	// --- Message types sent TO the compiler instance ---
	enum class MessageType
	{
		FinishedParsing,      // Parser completed parsing a source file
		RequestFileInclude    // Parser encountered an include directive and needs resolution
	};

	// --- Response types returned FROM the compiler instance ---
	enum class ResponseType
	{
		Success,  // Request handled successfully
		Error,    // Request failed with an error condition
		Pending   // Request cannot be completed yet (e.g., dependency not parsed)
	};

	struct CompilerMessage
	{
		uint32 sourceId;
		MessageType type;
		Core::String data;
	};

	struct CompilerResponse
	{
		uint32 sourceId;
		ResponseType type;
		Core::String data;
	};

	class CompilerMessageQueue
	{
	public:
		CompilerMessageQueue() = default;

		// Prevent copying to avoid shared state issues
		CompilerMessageQueue(const CompilerMessageQueue&) = delete;
		CompilerMessageQueue& operator=(const CompilerMessageQueue&) = delete;

		/**
		 * Send a message to the queue without waiting for a response.
		 * Notifies any threads waiting on the request queue.
		 */
		void SendMessage(uint32 sourceId, MessageType type, const Core::String& data)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_RequestQueue.Push(CompilerMessage{sourceId, type, data});
			m_RequestCV.notify_one();
		}

		/**
		 * Send a message and block until a response with the matching sourceId arrives.
		 * The calling thread waits on its own per-sourceId condition variable until FlushPendingResponse()
		 * is called for this sourceId, then dequeues and returns the response.
		 */
		CompilerResponse SendMessageAndAwaitResponse(uint32 sourceId, MessageType type, const Core::String& data)
		{
			std::unique_lock<std::mutex> lock(m_Mutex);

			// Push the request first
			m_RequestQueue.Push(CompilerMessage{sourceId, type, data});
			m_RequestCV.notify_one();

			// Get (or create) the per-sourceId condition variable, then wait on it.
			// cv->wait() releases m_Mutex during the wait and re-acquires it before
			// evaluating the predicate, so reading m_PendingResponses is safe.
			auto& cv = m_SourceCVs[sourceId];
			if (!cv)
				cv = std::make_shared<std::condition_variable>();
			cv->wait(lock, [this, sourceId]() {
				return m_PendingResponses.count(sourceId);
			});

			// Remove and return the response
			CompilerResponse response = m_PendingResponses[sourceId];
			m_PendingResponses.erase(sourceId);
			return response;
		}

		/**
		 * Push a response onto the pending response map without notifying.
		 * Called by the compiler instance (or any handler) to reply to a request.
		 * The response is delivered lazily: call FlushPendingResponse() when the
		 * requesting unit is about to be dispatched, or immediately for active units.
		 */
		void PushResponse(uint32 sourceId, ResponseType type, const Core::String& data)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_PendingResponses[sourceId] = CompilerResponse{sourceId, type, data};
		}

		/**
		 * If a pending response exists for the given sourceId, notify the waiting
		 * thread so it can wake up and consume it. Returns true if a response was
		 * flushed, false if none was pending.
		 */
		bool FlushPendingResponse(uint32 sourceId)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_PendingResponses.count(sourceId))
			{
				auto& cv = m_SourceCVs[sourceId];
				if (cv)
					cv->notify_one(); // wake only the thread waiting for this sourceId
				return true;
			}
			return false;
		}

		/**
		 * Dequeue the next request message. Returns nullopt if empty.
		 * Called by the compiler instance to process incoming messages.
		 */
		std::optional<CompilerMessage> PopRequest()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_RequestQueue.Empty())
				return std::nullopt;

			CompilerMessage msg = m_RequestQueue.Front();
			m_RequestQueue.Remove(static_cast<uint32>(0));
			return msg;
		}

		/**
		 * Block until a request message is available, then dequeue and return it.
		 * Called by the compiler instance when it needs to wait for exactly one message.
		 */
		CompilerMessage WaitPopRequest()
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_RequestCV.wait(lock, [this]() {
				return !m_RequestQueue.Empty();
			});

			CompilerMessage msg = m_RequestQueue.Front();
			m_RequestQueue.Remove(static_cast<uint32>(0));
			return msg;
		}

	private:
		mutable std::mutex m_Mutex;
		std::condition_variable m_RequestCV;                        // notifies compiler instance of new requests
		std::unordered_map<uint32, std::shared_ptr<std::condition_variable>> m_SourceCVs;  // per-sourceId CV for targeted response delivery

		Core::Vector<CompilerMessage> m_RequestQueue;       // messages sent TO the compiler instance
		std::unordered_map<uint32, CompilerResponse> m_PendingResponses;  // sourceId -> pending response (one per unit)
	};

}
