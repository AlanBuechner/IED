#include "FileIoTest.h"
#include "Compiler/FileIo.h"
#include <filesystem>
#include <optional>
#include <string>

namespace Testing
{
	using namespace Scripting;
	namespace fs = std::filesystem;

	namespace
	{
		// Windows' path limit for calls that do not take long paths.
		constexpr size_t kMaxPath = 260;

		fs::path Root() { return fs::temp_directory_path() / "c4_file_io_test"; }

		// The spelling of an absolute path that the standard library's calls take past MAX_PATH.
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
} // namespace Testing
