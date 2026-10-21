// Applies decoded events to a capture (docs/16 "Rules"). Nothing a client sends can stop a capture: an unbalanced
// ZoneEnd, a free of an address never allocated, a task id never begun become warnings and the capture goes on.
// The caller holds the capture's lock.

include "capture.ch"
include "decode.ch"

namespace Profiler
{
	void Apply(Capture& capture, const Event& event);

	// Decodes one chunk into the capture; a chunk that cannot be read whole becomes a warning.
	void ApplyChunk(Capture& capture, const uint8* chunk, int64 length);

	// The connection or file ended: zones and tasks still open are closed at the last time seen, and said so.
	void Finish(Capture& capture);
}
