// Importers (F-PRF-7, docs/16 "Trace files"): other tools' trace and profile formats turned into ITP streams, written
// as .iedtrace files the profiler opens like any capture. Chrome trace JSON (Chrome, Perfetto, Rust's tracing-chrome,
// Nuke's build trace, many engines) and pprof (Go's runtime/pprof, Rust's pprof-rs, gperftools) are read here; a
// language plugin adds others through the profilerImporters contribution (a command that writes one of these).

include "capture/Builder.ch"

namespace Profiler
{
	// The stream for a Chrome trace (a JSON object with traceEvents, or the array alone); false with `problem`.
	bool ImportChromeTrace(Core::StringView json, Core::StringView app, Core::Array<uint8>& stream, Core::String& problem);

	// The stream for a pprof profile (protobuf, gzip-compressed or not); false with `problem`.
	bool ImportPprof(const Core::Array<uint8>& bytes, Core::StringView app, Core::Array<uint8>& stream, Core::String& problem);

	// Converts the file `source` (by its contents: an .iedtrace is copied as it is) into a new .iedtrace in `folder`;
	// the file written, or empty with `problem`.
	Core::String ImportFile(Core::StringView source, Core::StringView folder, Core::String& problem);

	// The same for a file a contributed importer (`converter`) wrote from `origin`: the capture is named and described
	// after `origin`.
	Core::String ImportConverted(Core::StringView source, Core::StringView origin, Core::StringView converter, Core::StringView folder, Core::String& problem);
}
