// The process's standard output and standard error: the text is written whole and flushed at once, in order with
// printf.
//     Platform::WriteError(Core::StringView("warning: the cache is stale\n"));

include "Text/stringview.ch"

namespace Platform
{
	[link("c4_console_write")] int64 c4_console_write(int32 stream, const void* bytes, int64 count);

	// False when the stream is closed or the write failed.
	bool WriteOutput(Core::StringView text) { return c4_console_write(1, (const void*)text.Data(), text.ByteLength()) == text.ByteLength(); }
	bool WriteError(Core::StringView text) { return c4_console_write(2, (const void*)text.Data(), text.ByteLength()) == text.ByteLength(); }
}
