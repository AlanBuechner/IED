// Path: a file-system path as UTF-8 text (std::filesystem::path). Both '/' and '\' separate components when
// a path is read; Join writes '/', which every supported platform accepts. Everything here works on the text
// alone - nothing touches the file system, follows links or folds case.
//     FS::Path assets = FS::Path("game") / "assets";
//     FS::Path icon = assets / "icon.png";        // "game/assets/icon.png"
//     icon.Extension();                            // ".png"
//     icon.Parent().FileName();                    // "assets"

include "Container/array.ch"
include "Text/string.ch"

namespace FS
{
	class Path
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(const char* text) { this.text = Core::String(text); }
		[operator("create")] public void Init(Core::StringView text) { this.text = Core::String(text); }

		public const char* CStr() const { return text.CStr(); }
		public Core::StringView View() const { return text.View(); }
		public bool IsEmpty() const { return text.IsEmpty(); }
		// Starts at a root: "/a", "C:/a", "\\server\share\a". A drive alone ("C:a") is relative to that drive.
		public bool IsAbsolute() const;

		// The last component: "c.txt" of "a/b/c.txt", "" of "a/b/".
		public Core::StringView FileName() const;
		// The file name without its extension: "c" of "c.txt", ".bashrc" of ".bashrc".
		public Core::StringView Stem() const;
		// The file name from its last dot: ".txt" of "c.txt", "" of "c" and of ".bashrc".
		public Core::StringView Extension() const;
		// Without the last component and the separators before it: "a/b" of "a/b/c.txt", "" of "c.txt", "/" of "/c".
		public Path Parent() const;

		// `part` under this path with one separator between; `part` itself when it has a root.
		[operator("/")] public Path Join(Core::StringView part) const;
		[operator("/")] public Path Join(const char* part) const { return Join(Core::StringView(part)); }
		// The extension replaced, added, or removed when `extension` is empty; the dot is optional.
		public Path WithExtension(Core::StringView extension) const;
		// '/' separators, no empty or "." components, "name/.." folded away ("." when nothing is left).
		public Path Normalized() const;

		[operator("==")] public bool Equals(const Path& other) const { return text == other.text; }
		[operator("!=")] public bool NotEquals(const Path& other) const { return text != other.text; }
		[operator("hash")] public uint64 Hash() const { return text.Hash(); }

		private Core::String text;
	}

	bool IsSeparator(int32 byte);
	// The bytes of `path`'s root: 1 for "/", 3 for "C:/", 2 for "C:", through the share's separator for
	// "\\server\share\", 0 for a relative path.
	int64 RootLength(Core::StringView path);
	// The working directory of the process; an empty path when it cannot be read.
	Path CurrentDirectory();
	// The directory for temporary files; an empty path when there is none.
	Path TempDirectory();
}
