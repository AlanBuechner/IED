// StagedProgram (docs/24 "Staged launch"): a staged server's program is known by its size and time, and a server whose
// program was rebuilt starts again from a new copy once the program has settled.

include "bedrock.ch"

namespace Ied
{
	// How long a rebuilt program must stay as it is before its server starts again: a build still writing it changes it again.
	const int64 c_LspRebuiltSettleNs = 3000000000;

	// "<size>-<modified ns>" of the program at `program`; empty when there is none.
	Core::String StagedKeyOf(Core::StringView program);

	// Whether the program a server was started from was rebuilt since: a key other than the one it started from, seen alike
	// for the settle time. A missing program (a build removed it) is never a rebuild.
	class RebuildWatch
	{
		[operator("create")] public void Init() {}

		// The server started from the program with `key`.
		public void Started(Core::StringView key);
		// The program's key at `nowNs`: true once a key other than the started one has been seen alike for `settleNs`.
		public bool Rebuilt(Core::StringView key, int64 nowNs, int64 settleNs);

		private Core::String m_StartedFrom;
		private Core::String m_Seen;      // the other key seen last
		private int64 m_SeenSinceNs;      // since when it has been seen
	}
}
