// Annotations (F-PRF-19, docs/16 "Annotations"): notes on a capture's time ranges, by its owner or the AI, kept in
// its trace file's metadata so they travel with the file.

include "api/protocol.ch"

namespace Profiler
{
	const int64 kMostAnnotationBytes = 2000;
	const int32 kMostAnnotations = 500;

	class Annotation
	{
		public Core::String id;
		public int64 fromNs;
		public int64 toNs;              // fromNs for a note on a moment
		public Core::String text;
		public Core::String by;         // "owner" or "ai"
		public int64 createdUnixNs;
	}

	// The annotations a capture's metadata (its index block's JSON) holds; none for empty or malformed metadata.
	Core::Array<Annotation> ReadAnnotations(Core::StringView metadata);
	void WriteAnnotation(IED::JsonWriter& json, const Annotation& annotation);
	void WriteAnnotations(IED::JsonWriter& json, const Core::Array<Annotation>& annotations);
	// `metadata` with its annotations replaced (the member added when it has none, an object made when it is empty).
	Core::String MetadataWith(Core::StringView metadata, const Core::Array<Annotation>& annotations);
	// An id none of `annotations` has: "n" and one more than the largest number after an "n" there.
	Core::String NextAnnotationId(const Core::Array<Annotation>& annotations);
	// The annotation's text as given, trimmed; false with `problem` when it is empty or longer than the most kept.
	bool AnnotationText(Core::StringView given, Core::String& text, Core::String& problem);
}
