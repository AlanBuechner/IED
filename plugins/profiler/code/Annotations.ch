// Annotations (F-PRF-19, docs/16 "Annotations"): notes on a capture's time ranges, by its owner or the AI, kept in
// its trace file's metadata so they travel with the file.

include "api/Protocol.ch"

namespace Profiler
{
	const int64 c_MostAnnotationBytes = 2000;
	const int32 c_MostAnnotations = 500;

	class Annotation
	{
		public Core::String m_Id;
		public int64 m_FromNs;
		public int64 m_ToNs;              // fromNs for a note on a moment
		public Core::String m_Text;
		public Core::String m_By;         // "owner" or "ai"
		public int64 m_CreatedUnixNs;
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
