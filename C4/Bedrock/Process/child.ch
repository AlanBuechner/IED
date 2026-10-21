// Child: a program this one started (D-68). It owns the child's handles and closes them on delete, so it cannot be
// copied (move it with take()); a zero Child is closed. The child runs in a job: Kill ends it and every process it
// started, and with killOnClose (the default) so does closing the Child. A call that waits takes a timeout in
// milliseconds (kWaitForever, or 0 to only check); a call that fails keeps why in Error().
//     Process::StartInfo info;
//     info.program = Core::String("git");
//     info.arguments.Push(Core::String("status"));
//     info.output = Process::Redirect::Pipe;
//     Process::Child git = Process::Child::Start(info);
//     char[4096] text;
//     int64 read = git.Read(Process::Stream::Output, (void*)&text[0], 4096);
//     int64 exitCode = git.Wait();

include "Text/string.ch"
include "Text/stringview.ch"
include "Container/array.ch"

namespace Process
{
	// Besides a count or an exit code, a call returns one of these.
	const int64 kError = -1;
	const int64 kTimedOut = -2;
	// How long a call may wait: milliseconds, 0 to only check, or kWaitForever.
	const int32 kWaitForever = -1;

	// Where one of a child's standard streams goes: this process's own, a pipe this process writes (input) or reads,
	// nowhere, or (errors only) the same pipe as the output.
	enum class Redirect { Inherit = 0, Pipe = 1, Null = 2, ToOutput = 3 }

	enum class Stream { Input = 0, Output = 1, Errors = 2 }

	struct StartInfo
	{
		public Core::String program;                  // a path, or a name looked for on PATH (".exe" added); not a batch file
		public Core::Array<Core::String> arguments;   // each reaches the child as one argument, as written
		public Core::String workingDirectory;         // empty: this process's
		public Core::Array<Core::String> environment; // "NAME=value" sets a variable, "NAME" removes one
		public bool inheritEnvironment = true;        // false: `environment` is all the child has
		public Redirect input = Redirect::Inherit;
		public Redirect output = Redirect::Inherit;
		public Redirect errors = Redirect::Inherit;
		public bool terminal;                         // under a pseudo console: Input is its keyboard, Output its screen
		public int32 columns = 80;
		public int32 rows = 25;
		public bool killOnClose = true;
	}

	// The runtime's handles (c4_process).
	struct Handles
	{
		public void* process;
		public void* job;
		public void* input;
		public void* output;
		public void* errors;
		public void* console;
		public uint32 id;
	}

	class Child
	{
		[operator("create")] public void Init() {}   // closed
		[operator("delete")] public void Delete() { Close(); }

		// Starts `info.program`; a closed Child with Error() when it cannot (no such program, no such folder, ...).
		public static Child Start(const StartInfo& info);

		public bool IsOpen() const { return handles.process != nullptr; }
		// The system's id for the child; 0 when closed.
		public uint32 Id() const { return handles.id; }

		// Bytes written to the child's input (a terminal's keyboard), kTimedOut (part may have gone), or kError.
		public int64 Write(const void* bytes, int64 count, int32 timeoutMs = kWaitForever);
		public bool WriteAll(const void* bytes, int64 count, int32 timeoutMs = kWaitForever);
		public bool WriteText(Core::StringView text, int32 timeoutMs = kWaitForever) { return WriteAll((const void*)text.Data(), text.ByteLength(), timeoutMs); }
		// The end of the child's input.
		public void CloseInput();

		// Bytes read from Output or Errors, 0 once the child and what it started have closed it, kTimedOut, or kError.
		public int64 Read(Stream from, void* buffer, int64 capacity, int32 timeoutMs = kWaitForever);
		// Reads whichever of Output and Errors has bytes first and says which in `from`. 0 when one ends (it is closed
		// then, so the next call waits on the other) or when neither is open (HasOutput() is false).
		public int64 ReadAny(void* buffer, int64 capacity, Stream& from, int32 timeoutMs = kWaitForever);
		// Whether Output or Errors is still open to read.
		public bool HasOutput() const { return handles.output != nullptr || handles.errors != nullptr; }

		// The exit code once the child has ended (0 to 4294967295), kTimedOut, or kError.
		public int64 Wait(int32 timeoutMs = kWaitForever);
		// Ends the child and every process it started; its exit code is then 1.
		public bool Kill();
		// A terminal's new size; false for a child that is not one.
		public bool Resize(int32 columns, int32 rows);
		public void Close();   // idempotent

		public const char* Error() const { return error.CStr(); }
		public bool TimedOut() const { return timedOut; }

		private int64 Record(int64 result, const char* why);

		private Handles handles;
		private Core::String error;
		private bool timedOut;
	}
}
