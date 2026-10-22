// The process's standard streams. Output and error: the text is written whole and flushed at once, in order with
// printf. Input: a filter's (`tool < file`, a pipe) or a console's, read as it comes, all of it, or line by line.
// The streams carry the bytes as they are: a "\n" written is one byte, and a console's line arrives ending in "\r\n".
//     Platform::WriteError(Core::StringView("warning: the cache is stale\n"));
//     Platform::InputLines lines;
//     Core::String line;
//     while (lines.Next(line)) Platform::WriteOutput(line.View());

include "Text/string.ch"
include "Text/stringbuilder.ch"
include "Text/stringview.ch"

namespace Platform
{
	[link("c4_console_write")] int64 c4_console_write(int32 stream, const void* bytes, int64 count);
	[link("c4_console_read")] int64 c4_console_read(void* bytes, int64 capacity);

	// False when the stream is closed or the write failed.
	bool WriteOutput(Core::StringView text) { return c4_console_write(1, (const void*)text.Data(), text.ByteLength()) == text.ByteLength(); }
	bool WriteError(Core::StringView text) { return c4_console_write(2, (const void*)text.Data(), text.ByteLength()) == text.ByteLength(); }

	// Up to `capacity` bytes of standard input into `bytes`, as they come (a line from a console, what a pipe holds):
	// how many, 0 at its end, -1 when the read failed.
	int64 ReadInput(void* bytes, int64 capacity) { return c4_console_read(bytes, capacity); }

	// All of standard input, to its end, appended to `out`; false when a read failed.
	bool ReadAllInput(Core::StringBuilder& out);

	// Standard input a line at a time: each line without its "\n" or "\r\n", the last one also without a break.
	class InputLines
	{
		// The next line into `line`; false at the end of the input (or when a read failed).
		public bool Next(Core::String& line);

		private bool Fill();

		private Core::StringBuilder pending;   // read, from `start` not yet given
		private int64 start;
		private bool ended;
	}
}
