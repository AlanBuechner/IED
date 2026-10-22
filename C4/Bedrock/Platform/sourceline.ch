// SourceLineOf: where the instruction at a code address was written, its source file and line, as a profiler names
// a sampled frame or a crash report a return address. It reads the debug information (the PDB) of the module the
// address is in: a program built with -g, a debug script's DLL. Without one the line is not known.
//     Platform::SourceLine at = Platform::SourceLineOf(frame);
//     if (at.IsKnown()) printf("%s:%d\n", at.file.CStr(), at.line);

include "Text/string.ch"

namespace Platform
{
	struct SourceLine
	{
		public Core::String file;   // as the debug information names it, absolute
		public int32 line;          // 1 and up; 0 when not known
		public bool IsKnown() const { return line > 0; }
	}

	// Takes a lock and, the first time, loads the system's debug-information library: for naming addresses, not for
	// a hot loop.
	SourceLine SourceLineOf(const void* code);
}
