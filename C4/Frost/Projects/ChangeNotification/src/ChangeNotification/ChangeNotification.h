#pragma once
#include "Core/Core.h"
#include "Core/UUID.h"
#include <mutex>
#include <functional>
#include <vector>
#include <string>
#include <atomic>

namespace ChangeNotification
{
	// ------------------------------------------------------------------------
	// ChangeRecord
	// ------------------------------------------------------------------------
	struct ChangeRecord
	{
		Core::UUID ObjectID;
		void* ObjectPointer = nullptr;
		std::string ObjectPath;
		std::string PreviousValue;
		std::string NewValue;
		Core::UUID ChangeID;
	};

	// ------------------------------------------------------------------------
	// Glob-style path matching (supports * and ? wildcards)
	// ------------------------------------------------------------------------
	bool MatchGlob(const std::string& path, const std::string& pattern);

	namespace detail
	{
		bool MatchGlobRecursive(const char* str, const char* pattern);
	}

	// ------------------------------------------------------------------------
	// ChangeQueue — thread-safe singleton for change notifications
	// ------------------------------------------------------------------------
	using ChangeCallback = std::function<void(const ChangeRecord&)>;

	class ChangeQueue
	{
	public:
		// Singleton access
		static ChangeQueue& GetInstance();

		// Disable copy/move
		ChangeQueue(const ChangeQueue&) = delete;
		ChangeQueue& operator=(const ChangeQueue&) = delete;

		// Subscription with optional glob-style path filter.
		// An empty pattern (default) matches all change records.
		uint32 Subscribe(ChangeCallback callback, const std::string& pathFilter = "");

		// Remove a subscription by ID
		void Unsubscribe(uint32 id);

		// Enqueue a change record. In normal mode this immediately invokes
		// matching callbacks on the calling thread. In batch mode records
		// are accumulated until ProcessPendingNotifications is called.
		void Notify(const ChangeRecord& record);

		// Batch mode control
		void SetBatchMode(bool enabled);
		bool IsBatchMode() const { return m_BatchMode.load(std::memory_order_relaxed); }

		// Process all accumulated batch records and invoke callbacks.
		// No-op if batch mode is disabled.
		void ProcessPendingNotifications();

	private:
		struct Subscription
		{
			uint32 id;
			ChangeCallback callback;
			std::string pathFilter; // empty means match-all
		};

		ChangeQueue() = default;

		bool MatchesFilter(const std::string& filter, const std::string& objectPath);
		void InvokeMatching(const ChangeRecord& record);

		std::mutex m_Mutex;
		std::vector<Subscription> m_Subscriptions;
		uint32 m_NextID = 1;

		std::atomic<bool> m_BatchMode{ false };
		std::vector<ChangeRecord> m_PendingRecords;
	};

	// ------------------------------------------------------------------------
	// ObservableProperty<T> — emits ChangeRecord only on actual change
	// ------------------------------------------------------------------------
	template<typename T>
	class ObservableProperty
	{
	public:
		explicit ObservableProperty(const T& initialValue, const std::string& objectPath = "", void* objectPointer = nullptr)
			: m_Value(initialValue)
			, m_ObjectPath(objectPath)
			, m_ObjectPointer(objectPointer)
		{
		}

		// Get the current value
		const T& Get() const { return m_Value; }
		T& GetMutable() { return m_Value; }

		// Set a new value. Emits a change record only if the value actually differs.
		void Set(const T& newValue)
		{
			if (m_Value == newValue)
				return; // No actual change — suppress notification

			T previousValue = m_Value;
			m_Value = newValue;

			ChangeRecord record;
			record.ObjectID = Core::UUID::Create();
			record.ObjectPointer = m_ObjectPointer;
			record.ObjectPath = m_ObjectPath;
			record.PreviousValue = Serialize(previousValue);
			record.NewValue = Serialize(newValue);
			record.ChangeID = Core::UUID::Create();

			ChangeQueue::GetInstance().Notify(record);
		}

		// Implicit conversion to T for natural access
		operator const T&() const { return m_Value; }

	private:
		std::string Serialize(const T& value)
		{
			if constexpr (std::is_same_v<T, std::string>)
				return value;
			else if constexpr (std::is_arithmetic_v<T>)
				return std::to_string(value);
			else
				return "<serialized>"; // Fallback — specialise as needed
		}

		T m_Value;
		std::string m_ObjectPath;
		void* m_ObjectPointer = nullptr;
	};
}
