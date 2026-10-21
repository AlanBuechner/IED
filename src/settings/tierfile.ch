// TierFile (docs/07 "Tier files"): one settings file - its text, its revision, the last valid value of each key it
// sets, and what is wrong with it - read again whenever it changes.

include "catalog.ch"
include "jsonedit.ch"

namespace Ied
{
	class TierProblem
	{
		public int32 line;
		public Core::String key;      // empty for a problem of the whole file
		public Core::String message;
	}

	class TierFile
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Core::StringView tier, const FS::Path& file) { this.tier = Core::String(tier); this.file = file; }

		// Reads the file and checks it against `catalog`: false when its text is what it was (nothing to tell).
		// `force` checks it again anyway (the catalog changed).
		public bool Reload(SettingsCatalog& catalog, bool force);
		// Replaces the file's text atomically (its folder made), then reads it back; false with `error`.
		public bool Write(SettingsCatalog& catalog, Core::StringView text, Core::String& error);
		// {file, revision, problems: [{line, key?, message}]}
		public Core::String StateJson() const;

		public Core::String tier;
		public FS::Path file;
		public int64 revision;
		public Core::String text;
		public Core::HashMap<Core::String, Core::String> values;   // key -> its last valid value, JSON
		public Core::Array<TierProblem*> problems;

		private void Check(SettingsCatalog& catalog);
		private void Problem(int32 line, Core::StringView key, Core::StringView message);
		private bool loaded;
	}
}
