// ChangeBatch: the changes a watcher reported for a while, folded to one per path (docs/10 tree.changed): created then
// deleted is nothing, deleted then created is changed, and a rename is the old path deleted and the new one created.
//     batch.Add(change);
//     for (const TreeChange& each : batch.Changes()) ...
// A path is found through an index, so a burst of thousands of changes folds in linear time.

include "bedrock.ch"

namespace Ied
{
	enum class TreeChangeKind { Created, Changed, Deleted }

	struct TreeChange
	{
		public Core::String m_Path;   // workspace-relative, '/' separators
		public TreeChangeKind m_Kind;
	}

	class ChangeBatch
	{
		public void Add(const FS::Change& change);
		public bool IsEmpty() const { return m_LiveCount == 0 && !m_Overflow; }
		// The changes in the order their paths first changed.
		public Core::Array<TreeChange> Changes() const;
		// The system dropped changes: the folder must be looked at again.
		public bool Overflowed() const { return m_Overflow; }
		public void Clear();

		private void Fold(Core::StringView path, FS::ChangeKind kind);

		private Core::Array<TreeChange> m_Entries;
		private Core::Array<bool> m_Live;                  // false: the entry's changes cancelled out
		private Core::HashMap<Core::String, int64> m_Index;   // path -> entry
		private int64 m_LiveCount;
		private bool m_Overflow;
	}

	// What `kind` happening after `before` amounts to; false when the two cancel out.
	bool FoldChange(TreeChangeKind before, FS::ChangeKind kind, TreeChangeKind& after);
	TreeChangeKind FirstChange(FS::ChangeKind kind);
	const char* TreeChangeName(TreeChangeKind kind);
	// A watcher's path with '/' separators.
	Core::String WatchedPath(Core::StringView path);
}
