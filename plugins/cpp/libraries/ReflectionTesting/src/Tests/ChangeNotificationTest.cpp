#include "ChangeNotificationTest.h"
#include <vector>
#include <string>

namespace Testing
{
	// ── ObservableProperty emission behavior ────────────────────────────────

	void ChangeNotificationTest::TestObservablePropertyEmission()
	{
		// Verify that setting a new value emits a ChangeRecord with correct fields.
		std::vector<ChangeNotification::ChangeRecord> received;
		
		auto id = ChangeNotification::ChangeQueue::GetInstance().Subscribe(
			[&](const ChangeNotification::ChangeRecord& rec) {
				received.push_back(rec);
			});

		ChangeNotification::ObservableProperty<int> prop(0, "entity/health", nullptr);
		prop.Set(42);

		ChangeNotification::ChangeQueue::GetInstance().Unsubscribe(id);

		if (received.size() != 1) {
			Core::String msg = Core::FormatString("Expected 1 notification, got {}", received.size());
			TEST_FAILED(msg);
		}

		if (received[0].ObjectPath != "entity/health") {
			Core::String msg = Core::FormatString("Expected path 'entity/health', got '{}'", received[0].ObjectPath);
			TEST_FAILED(msg);
		}

		if (received[0].PreviousValue != "0") {
			Core::String msg = Core::FormatString("Expected previous value '0', got '{}'", received[0].PreviousValue);
			TEST_FAILED(msg);
		}

		if (received[0].NewValue != "42") {
			Core::String msg = Core::FormatString("Expected new value '42', got '{}'", received[0].NewValue);
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	// ── Duplicate suppression ───────────────────────────────────────────────

	void ChangeNotificationTest::TestDuplicateSuppression()
	{
		// Setting to the same value should not emit any notification.
		std::vector<ChangeNotification::ChangeRecord> received;

		auto id = ChangeNotification::ChangeQueue::GetInstance().Subscribe(
			[&](const ChangeNotification::ChangeRecord&) {
				received.push_back(ChangeNotification::ChangeRecord{});
			});

		ChangeNotification::ObservableProperty<int> prop(10, "test/value", nullptr);
		prop.Set(10); // Same value — should be suppressed
		prop.Set(10); // Again same value

		ChangeNotification::ChangeQueue::GetInstance().Unsubscribe(id);

		if (!received.empty()) {
			Core::String msg = Core::FormatString("Expected 0 notifications for duplicate values, got {}", received.size());
			TEST_FAILED(msg);
		}

		// Now change to a different value — should emit.
		received.clear();
		id = ChangeNotification::ChangeQueue::GetInstance().Subscribe(
			[&](const ChangeNotification::ChangeRecord&) {
				received.push_back(ChangeNotification::ChangeRecord{});
			});

		prop.Set(20); // Different value — should emit

		ChangeNotification::ChangeQueue::GetInstance().Unsubscribe(id);

		if (received.size() != 1) {
			Core::String msg = Core::FormatString("Expected 1 notification for actual change, got {}", received.size());
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	// ── Subscription and unsubscription lifecycle ───────────────────────────

	void ChangeNotificationTest::TestSubscribeUnsubscribeLifecycle()
	{
		// Verify unique subscription IDs, callback invocation, and removal.
		auto& queue = ChangeNotification::ChangeQueue::GetInstance();

		// Each subscription should return a unique ID.
		uint32 id1 = queue.Subscribe([](const ChangeNotification::ChangeRecord&) {});
		uint32 id2 = queue.Subscribe([](const ChangeNotification::ChangeRecord&) {});
		uint32 id3 = queue.Subscribe([](const ChangeNotification::ChangeRecord&) {});

		if (id1 == id2 || id2 == id3 || id1 == id3) {
			TEST_FAILED("Subscription IDs are not unique");
		}

		// Verify callback is invoked after subscription.
		int callCount = 0;
		uint32 callId = queue.Subscribe([&](const ChangeNotification::ChangeRecord&) {
			callCount++;
		});

		ChangeNotification::ChangeRecord rec;
		rec.ObjectPath = "lifecycle/test";
		queue.Notify(rec);

		if (callCount != 1) {
			Core::String msg = Core::FormatString("Expected 1 callback invocation, got {}", callCount);
			queue.Unsubscribe(callId);
			TEST_FAILED(msg);
		}

		// Unsubscribe and verify callback is no longer invoked.
		queue.Unsubscribe(callId);
		queue.Notify(rec);

		if (callCount != 1) {
			Core::String msg = Core::FormatString("Expected no invocation after unsubscribe, count is {}", callCount);
			TEST_FAILED(msg);
		}

		// Clean up.
		queue.Unsubscribe(id1);
		queue.Unsubscribe(id2);
		queue.Unsubscribe(id3);

		TEST_PASSED();
	}

	// ── Exact path filter matching ──────────────────────────────────────────

	void ChangeNotificationTest::TestExactPathFilter()
	{
		// A subscriber with an exact path filter should only receive matching records.
		std::vector<std::string> receivedPaths;

		auto id = ChangeNotification::ChangeQueue::GetInstance().Subscribe(
			[&](const ChangeNotification::ChangeRecord& rec) {
				receivedPaths.push_back(rec.ObjectPath);
			},
			"exact/path"); // Exact filter — no wildcards

		ChangeNotification::ChangeRecord rec;

		rec.ObjectPath = "exact/path";
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		rec.ObjectPath = "exact/other";
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		rec.ObjectPath = "other/path";
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		ChangeNotification::ChangeQueue::GetInstance().Unsubscribe(id);

		if (receivedPaths.size() != 1) {
			Core::String msg = Core::FormatString("Expected 1 matching notification, got {}", receivedPaths.size());
			TEST_FAILED(msg);
		}

		if (receivedPaths[0] != "exact/path") {
			Core::String msg = Core::FormatString("Expected 'exact/path', got '{}'", receivedPaths[0]);
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	// ── Wildcard path filter matching (* and ?) ─────────────────────────────

	void ChangeNotificationTest::TestWildcardPathFilter()
	{
		// Verify that * (zero or more chars) and ? (exactly one char) work in filters.
		std::vector<std::string> receivedPaths;

		auto id = ChangeNotification::ChangeQueue::GetInstance().Subscribe(
			[&](const ChangeNotification::ChangeRecord& rec) {
				receivedPaths.push_back(rec.ObjectPath);
			},
			"entity/*/health"); // Star wildcard

		ChangeNotification::ChangeRecord rec;

		rec.ObjectPath = "entity/player/health";
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		rec.ObjectPath = "entity/enemy/health";
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		rec.ObjectPath = "entity/boss/armor"; // Should NOT match — last segment is armor, not health
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		rec.ObjectPath = "world/player/health"; // Should NOT match — first segment is world, not entity
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		ChangeNotification::ChangeQueue::GetInstance().Unsubscribe(id);

		if (receivedPaths.size() != 2) {
			Core::String msg = Core::FormatString("Expected 2 matching wildcard notifications, got {}", receivedPaths.size());
			TEST_FAILED(msg);
		}

		if (receivedPaths[0] != "entity/player/health" || receivedPaths[1] != "entity/enemy/health") {
			Core::String msg = Core::FormatString("Unexpected paths: '{}', '{}'", receivedPaths[0], receivedPaths[1]);
			TEST_FAILED(msg);
		}

		// Now test ? wildcard (exactly one character).
		receivedPaths.clear();
		id = ChangeNotification::ChangeQueue::GetInstance().Subscribe(
			[&](const ChangeNotification::ChangeRecord& rec) {
				receivedPaths.push_back(rec.ObjectPath);
			},
			"a?c"); // Should match "abc" but not "abbc" or "ac"

		rec.ObjectPath = "abc";
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		rec.ObjectPath = "abbc";
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		rec.ObjectPath = "ac";
		ChangeNotification::ChangeQueue::GetInstance().Notify(rec);

		ChangeNotification::ChangeQueue::GetInstance().Unsubscribe(id);

		if (receivedPaths.size() != 1) {
			Core::String msg = Core::FormatString("Expected 1 matching ? wildcard notification, got {}", receivedPaths.size());
			TEST_FAILED(msg);
		}

		if (receivedPaths[0] != "abc") {
			Core::String msg = Core::FormatString("Expected 'abc', got '{}'", receivedPaths[0]);
			TEST_FAILED(msg);
		}

		TEST_PASSED();
	}

	// ── Batch accumulation mode ─────────────────────────────────────────────

	void ChangeNotificationTest::TestBatchAccumulation()
	{
		// In batch mode, Notify should accumulate records without invoking callbacks immediately.
		auto& queue = ChangeNotification::ChangeQueue::GetInstance();

		int callCount = 0;
		uint32 id = queue.Subscribe([&](const ChangeNotification::ChangeRecord&) {
			callCount++;
		});

		queue.SetBatchMode(true);

		if (!queue.IsBatchMode()) {
			queue.Unsubscribe(id);
			TEST_FAILED("Batch mode was not enabled");
		}

		// Notify several records while in batch mode.
		ChangeNotification::ChangeRecord rec;
		rec.ObjectPath = "batch/1";
		queue.Notify(rec);

		rec.ObjectPath = "batch/2";
		queue.Notify(rec);

		rec.ObjectPath = "batch/3";
		queue.Notify(rec);

		// Callbacks should NOT have been invoked yet.
		if (callCount != 0) {
			Core::String msg = Core::FormatString("Expected 0 invocations in batch mode, got {}", callCount);
			queue.Unsubscribe(id);
			queue.SetBatchMode(false);
			TEST_FAILED(msg);
		}

		// Drain pending notifications.
		queue.ProcessPendingNotifications();

		if (callCount != 3) {
			Core::String msg = Core::FormatString("Expected 3 invocations after drain, got {}", callCount);
			queue.Unsubscribe(id);
			queue.SetBatchMode(false);
			TEST_FAILED(msg);
		}

		// Clean up.
		queue.SetBatchMode(false);
		queue.Unsubscribe(id);

		TEST_PASSED();
	}

	// ── ProcessPendingNotifications drain ───────────────────────────────────

	void ChangeNotificationTest::TestPendingDrain()
	{
		// Verify that ProcessPendingNotifications processes accumulated records in order,
		// and that a second call after draining produces no additional invocations.
		auto& queue = ChangeNotification::ChangeQueue::GetInstance();

		std::vector<std::string> receivedPaths;
		uint32 id = queue.Subscribe([&](const ChangeNotification::ChangeRecord& rec) {
			receivedPaths.push_back(rec.ObjectPath);
		});

		queue.SetBatchMode(true);

		// Accumulate several records.
		ChangeNotification::ChangeRecord rec;
		for (int i = 0; i < 5; i++) {
			rec.ObjectPath = Core::FormatString("drain/{}", i);
			queue.Notify(rec);
		}

		// First drain — should process all 5.
		queue.ProcessPendingNotifications();

		if (receivedPaths.size() != 5) {
			Core::String msg = Core::FormatString("Expected 5 records after first drain, got {}", receivedPaths.size());
			queue.Unsubscribe(id);
			queue.SetBatchMode(false);
			TEST_FAILED(msg);
		}

		// Verify order is preserved.
		for (int i = 0; i < 5; i++) {
			std::string expected = Core::FormatString("drain/{}", i);
			if (receivedPaths[i] != expected) {
				Core::String msg = Core::FormatString("Expected '{}', got '{}' at index {}", expected, receivedPaths[i], i);
				queue.Unsubscribe(id);
				queue.SetBatchMode(false);
				TEST_FAILED(msg);
			}
		}

		// Second drain — should produce no additional invocations (nothing pending).
		size_t countBefore = receivedPaths.size();
		queue.ProcessPendingNotifications();

		if (receivedPaths.size() != countBefore) {
			Core::String msg = Core::FormatString(
				"Expected no additional records on second drain, got {} total", receivedPaths.size());
			queue.Unsubscribe(id);
			queue.SetBatchMode(false);
			TEST_FAILED(msg);
		}

		// Clean up.
		queue.SetBatchMode(false);
		queue.Unsubscribe(id);

		TEST_PASSED();
	}
}
