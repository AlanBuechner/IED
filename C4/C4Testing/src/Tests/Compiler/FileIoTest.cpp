#include "FileIoTest.h"
#include "Compiler/FileIo.h"
#include <filesystem>
#include <optional>
#include <process.h>
#include <string>

namespace Testing
{
	using namespace Scripting;
	namespace fs = std::filesystem;

	namespace
	{
		// Windows' path limit for calls that do not take long paths.
		constexpr size_t kMaxPath = 260;

		// This process's own: test runs at the same time never share it.
		fs::path Root() { return fs::temp_directory_path() / ("c4_file_io_test_" + std::to_string(_getpid())); }

		// The spelling of an absolute path that the standard library's calls take past MAX_PATH, written out (what
		// ExtendedPath must give).
		fs::path LongPath(const fs::path& path) { return fs::path(L"\\\\?\\" + path.wstring()); }

		// A fresh folder deep enough that a file's path in it is longer than MAX_PATH.
		fs::path DeepFolder()
		{
			std::error_code ignored;
			fs::remove_all(LongPath(Root()), ignored);
			const fs::path folder = Root() / std::string(120, 'd') / std::string(120, 'e');
			fs::create_directories(LongPath(folder));
			return folder;
		}

		void RemoveRoot()
		{
			std::error_code ignored;
			fs::remove_all(LongPath(Root()), ignored);
		}
	}

	void FileIoTest::ADeepFileReadsBackWhatWasWritten()
	{
		const std::string path = (DeepFolder() / "bindings.c4bind").string();
		TEST_ASSERT(path.size() > kMaxPath);
		const std::string bytes = std::string("line one\r\nline two\n") + '\0' + "after a zero";
		TEST_ASSERT(WriteFileBytes(path, bytes));
		TEST_ASSERT(FileExists(path));
		TEST_ASSERT_EQ(ReadFileBytes(path), bytes);
		const std::optional<FileStamp> stamp = StampOf(path);
		TEST_ASSERT(stamp.has_value());
		TEST_ASSERT_EQ(stamp->size, uint64_t(bytes.size()));
		TEST_ASSERT(stamp->modified > 0);
		TEST_ASSERT(WriteFileBytes(path, "shorter"));
		TEST_ASSERT_EQ(ReadFileBytes(path), std::string("shorter")); // the whole file, not over the old bytes
		RemoveFile(path);
		TEST_ASSERT(!FileExists(path));
		RemoveRoot();
	}

	void FileIoTest::AMissingFileHasNoBytesAndNoStamp()
	{
		const std::string path = (Root() / "missing.bin").string();
		RemoveRoot();
		TEST_ASSERT(!FileExists(path));
		TEST_ASSERT(ReadFileBytes(path).empty());
		TEST_ASSERT(!StampOf(path).has_value());
		RemoveFile(path); // nothing to remove is not an error
	}

	void FileIoTest::AFileInAMissingFolderIsNotWritten()
	{
		RemoveRoot();
		const std::string path = (Root() / "no-such-folder" / "out.key").string();
		TEST_ASSERT(!WriteFileBytes(path, "key"));
		TEST_ASSERT(!FileExists(path));
	}

	void FileIoTest::DeepFilesAndFoldersAreTold()
	{
		const fs::path folder = DeepFolder();
		const std::string file = PathText(folder / "kinds.cf");
		TEST_ASSERT(file.size() > kMaxPath);
		TEST_ASSERT(WriteFileBytes(file, "int32 x;"));
		TEST_ASSERT(IsFile(file) && !IsFolder(file));
		TEST_ASSERT(IsFolder(PathText(folder)) && !IsFile(PathText(folder)));
		TEST_ASSERT(!IsFile(PathText(folder / "missing.cf")) && !IsFolder(PathText(folder / "missing")));
		std::error_code ec;
		std::size_t listed = 0;
		for (fs::directory_iterator it(ExtendedPath(folder), ec), end; !ec && it != end; it.increment(ec))
			listed += PlainPath(it->path()) == folder / "kinds.cf" ? 1 : 0;
		TEST_ASSERT_EQ(listed, std::size_t(1));
		RemoveRoot();
	}

	void FileIoTest::ExtendedPathsAreSpelledForStdFilesystem()
	{
		TEST_ASSERT(ExtendedPath(L"C:\\a\\b\\..\\c/d") == fs::path(L"\\\\?\\C:\\a\\c\\d"));
		TEST_ASSERT(ExtendedPath(L"\\\\server\\share\\x") == fs::path(L"\\\\?\\UNC\\server\\share\\x"));
		TEST_ASSERT(ExtendedPath(L"\\\\?\\C:\\as\\is") == fs::path(L"\\\\?\\C:\\as\\is"));
		TEST_ASSERT(ExtendedPath(Root()) == LongPath(Root().lexically_normal()));
		TEST_ASSERT(PlainPath(L"\\\\?\\C:\\a\\c") == fs::path(L"C:\\a\\c"));
		TEST_ASSERT(PlainPath(L"\\\\?\\UNC\\server\\share\\x") == fs::path(L"\\\\server\\share\\x"));
		TEST_ASSERT(PlainPath(L"C:\\plain") == fs::path(L"C:\\plain"));
		TEST_ASSERT(PathText(fs::path(L"C:\\caf\u00e9")) == "C:\\caf\xc3\xa9");
	}
} // namespace Testing
