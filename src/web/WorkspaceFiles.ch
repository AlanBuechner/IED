// A workspace's files over HTTP (docs/10 "GET /w/<wsId>/files/<path>"): what the file viewers show - pictures, sound,
// video, PDFs, fonts - as their types, any other file as bytes to save; a byte range when one is asked for, so a video
// seeks and the hex viewer reads only the rows it shows. Nothing served here runs script on IED's origin: every type
// but a PDF goes with a sandboxing Content-Security-Policy, and nothing is sniffed.
//     Ied::ServeWorkspaceFile(FS::Path("C:/game/shots/a.png"), request, false, response);

include "bedrock.ch"

namespace Ied
{
	const int64 c_FileRangeMostBytes = 4194304;     // a range answers at most 4 MiB; the client asks again for the rest
	const int64 c_FileWholeMostBytes = 268435456;   // without a range, a file of at most 256 MiB is sent

	enum class RangeAsked { Whole, Part, Unsatisfiable }

	// The type a workspace file is served as, by its extension in any case; "application/octet-stream" for one no
	// viewer shows.
	const char* WorkspaceFileType(Core::StringView extension);

	// What a Range header's value asks of a file of `size` bytes: Part with `first`..`last` (inclusive, at most
	// c_FileRangeMostBytes of them) for one byte range - "bytes=0-499", "bytes=500-", "bytes=-500" -, Unsatisfiable for
	// one that starts past the end, Whole for anything else (several ranges, another unit, a malformed value).
	RangeAsked ParseByteRange(Core::StringView header, int64 size, int64& first, int64& last);

	// A GET of `file` (it exists): its bytes or the range asked, with its type, ETag and Accept-Ranges; 304 for an
	// If-None-Match naming its ETag; 413 for a file too large to send without a range. With `download`, or for a type no
	// viewer shows, it comes as an attachment to save.
	void ServeWorkspaceFile(const FS::Path& file, const Http::Request& request, bool download, Http::Response& response);
}
