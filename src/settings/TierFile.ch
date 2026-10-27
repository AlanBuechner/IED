// TierFile (docs/07 "Tier files"): one settings file - its text, its revision, the last valid value of each key it
// sets, and what is wrong with it - read again whenever it changes.

include "Catalog.ch"
include "JsonEdit.ch"

namespace Ied
{
	class TierProblem
	{
		public int32 m_Line;
		public Core::String m_Key;      // empty for a problem of the whole file
		public Core::String m_Message;
	}

	class TierFile
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Core::StringView tier, const FS::Path& file) { this.m_Tier = Core::String(tier); this.m_File = file; }

		// Reads the file and checks it against `catalog`: false when its text is what it was (nothing to tell).
		// `force` checks it again anyway (the catalog changed).
		public bool Reload(SettingsCatalog& catalog, bool force);
		// Replaces the file's text atomically (its folder made), then reads it back; false with `error`.
		public bool Write(SettingsCatalog& catalog, Core::StringView text, Core::String& error);
		// {file, revision, problems: [{line, key?, message}]}
		public Core::String StateJson() const;

		public Core::String m_Tier;
		public FS::Path m_File;
		public int64 m_Revision;
		public Core::String m_Text;
		public Core::HashMap<Core::String, Core::String> m_Values;   // key -> its last valid value, JSON
		public Core::Array<TierProblem*> m_Problems;

		private void Check(SettingsCatalog& catalog);
		private void Problem(int32 line, Core::StringView key, Core::StringView message);
		private bool m_Loaded;
	}
}
