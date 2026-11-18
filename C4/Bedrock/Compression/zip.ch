// ZIP archives (PKWARE's APPNOTE). ZipReader lists an archive's entries from its central directory and reads one entry
// at a time from the file - stored or deflated, its CRC-32 checked - so a large archive costs only the entries read.
// ZipWriter writes one entry after another (deflated when that is smaller, else stored) and the directory at Finish.
// ZIP64 archives (past 4 GiB or 65535 entries), split archives, encrypted entries and other methods are refused with
// a reason.
//     Compression::ZipReader zip;
//     if (!zip.Open("chapter.cbz")) printf("%s\n", zip.Error().Data());
//     Core::Array<uint8> page;
//     for (const Compression::ZipEntry& entry : zip.Entries())
//         if (!entry.IsFolder() && zip.Read(entry, page)) ...
//     Compression::ZipWriter out;
//     if (out.Create("pages.cbz") && out.Add("001.jpg", bytes, count, Compression::kStoreLevel)) out.Finish();

include "inflate.ch"
include "deflate.ch"
include "Container/crc32.ch"
include "FS/file.ch"

namespace Compression
{
	const int32 kZipStored = 0;
	const int32 kZipDeflated = 8;

	struct ZipEntry {
		public Core::String name;      // its path in the archive, '/' between folders; a folder's ends in '/'
		public int64 size;             // its data's bytes
		public int64 compressedSize;   // the bytes it takes in the archive
		public uint32 crc;             // CRC-32 of its data
		public int32 method;           // kZipStored or kZipDeflated; Read refuses others
		public bool encrypted;         // Read refuses it
		public int64 headerOffset;     // where its local header starts

		public bool IsFolder() const;
	}

	class ZipReader {
		[operator("create")] public void Init() {}

		// The archive's entries, read from its central directory; false, with Error(), for a file that cannot be read or
		// is not a ZIP archive this reads (damaged, ZIP64, split).
		public bool Open(const char* path);
		public void Close();
		public const Core::Array<ZipEntry>& Entries() const { return entries; }
		// The entry named `name` exactly; null when there is none.
		public const ZipEntry* Find(Core::StringView name) const;
		// The entry's data in `out` (replacing what it held); false, with Error(), when it cannot be read, its method is
		// not one this reads, or its data does not match its size and CRC-32.
		public bool Read(const ZipEntry& entry, Core::Array<uint8>& out);
		// Why the last Open or Read failed.
		public Core::StringView Error() const { return error.View(); }

		private bool Fail(const char* why);
		private bool ReadDirectory(int64 offset, int64 size, int64 count);

		private FS::File file;
		private Core::Array<ZipEntry> entries;
		private Core::String error;
	}

	class ZipWriter {
		[operator("create")] public void Init() {}

		// A new archive at `path`, replacing a file there; false, with Error(), when it cannot be written.
		public bool Create(const char* path);
		// `size` bytes at `data` as the entry `name` ('/' between folders), deflated at `level` (deflate.ch) when that is
		// smaller, stored otherwise or at kStoreLevel. False, with Error(), on a write failure or past ZIP's 4 GiB and
		// 65535 entries without ZIP64.
		public bool Add(Core::StringView name, const uint8* data, int64 size, int32 level);
		// The central directory written and the file closed: the archive is whole only after it.
		public bool Finish();
		// The file closed without its directory (an archive given up): what was written is not a ZIP archive.
		public void Close() { file.Close(); }
		public Core::StringView Error() const { return error.View(); }

		private bool Fail(const char* why);
		private bool WriteBytes(const Core::Array<uint8>& bytes);

		private FS::File file;
		private Core::Array<ZipEntry> entries;
		private int64 offset;          // where the next local header goes
		private Core::String error;
	}
}
