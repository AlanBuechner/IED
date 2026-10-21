// Topics (docs/10): each numbers its events, keeps the latest in a bounded ring for replay, and gives a snapshot of
// its state; the hub finds topics by key and hands each event to the sessions subscribed to its topic. Everything here
// runs with the server lock held.
//     Ied::Topic* topic = hub.Add(Core::StringView("doc:d1"), [doc]() -> Core::String { return doc->Snapshot(); });
//     hub.Publish(Core::StringView("doc:d1"), "doc.saved", data.View());

include "json.ch"

namespace Ied
{
	// Coalescing (docs/10): an event of a coalescible topic whose data is one growing string - `field`, beside other
	// members that stay the same (`rest`, JSON members without braces) - merges into the next one queued for a slow
	// client.
	struct Merge
	{
		public Core::String field;
		public Core::String text;
		public Core::String rest;
	}

	class Event
	{
		public int64 seq;
		public Core::String kind;
		public Core::String text;   // the whole evt frame
		public int64 bytes;
		public bool mergeable;
		public Merge merge;
	}

	// A topic's state as JSON text.
	alias Snapshot = func<Core::String()>;

	struct TopicLimits
	{
		public int64 events;        // the ring keeps at most this many events
		public int64 bytes;         // and this many bytes of them
		public bool coalescible;
	}

	class Topic
	{
		public Core::String key;
		public Snapshot snapshot;
		public TopicLimits limits;
		public int64 seq;
		public Core::Queue<Event*> ring;
		public int64 ringBytes;
		public Core::Array<Session*> subscribers;

		// The oldest event the ring holds (seq + 1 when it holds none).
		public int64 FirstSeq() const { return ring.IsEmpty() ? seq + 1 : ring.Front()->seq; }
		// Whether the ring still holds every event after `cursor`.
		public bool CanReplayFrom(int64 cursor) const { return cursor <= seq && cursor + 1 >= FirstSeq(); }
		public int64 ReplayBytes(int64 cursor) const;
		public Core::Array<Event*> EventsAfter(int64 cursor) const;
		public Core::String SnapshotFrame() const;
		public void Append(Event* event);
		public void ClearRing();
		public void Subscribe(Session* session);
		public void Unsubscribe(Session* session);
	}

	class Hub
	{
		// A topic with the ring limits of its kind (docs/10); nullptr when the key is taken.
		public Topic* Add(Core::StringView key, Snapshot snapshot) { return Add(key, snapshot, DefaultLimits(key)); }
		public Topic* Add(Core::StringView key, Snapshot snapshot, TopicLimits limits);
		public Topic* Find(Core::StringView key) const;
		// Publishes an event with `data` (a JSON object) to every subscriber; its seq.
		public int64 Publish(Core::StringView key, const char* kind, Core::StringView data);
		// An event whose data is `merge` (coalescible on a coalescible topic).
		public int64 PublishMergeable(Core::StringView key, const char* kind, const Merge& merge);
		public void Remove(Core::StringView key);
		public void ClearRings();
		public static TopicLimits DefaultLimits(Core::StringView key);
		// `hear(topic, kind, data)` for every event published from now on (plugin code listens through it, docs/26).
		public void Tap(func<void(Core::StringView, Core::StringView, Core::StringView)> hear) {
			tap = hear;
			tapped = true;
		}

	private:
		private int64 Deliver(Topic* topic, Event* event);

		private func<void(Core::StringView, Core::StringView, Core::StringView)> tap;
		private bool tapped;

		private Core::HashMap<Core::String, Topic*> topics;
	}

	// The kind of a topic key: "chat" for "chat:42", "stream" for "testhooks/stream:3".
	Core::StringView TopicKind(Core::StringView key);
	// A merge's data object as JSON text.
	Core::String MergeData(const Merge& merge);
	// An evt frame.
	Core::String EventFrame(Core::StringView topic, int64 seq, Core::StringView kind, Core::StringView data);
}
