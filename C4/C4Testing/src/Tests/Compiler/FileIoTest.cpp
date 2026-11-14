#include "FileIoTest.h"
#include "Compiler/FileIo.h"
#include <llvm/Support/Process.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Testing
{
	using namespace Scripting;
	namespace fs = std::filesystem;

	namespace
	{
		// Windows' path limit for calls that do not take long paths.
		constexpr size_t kMaxPath = 260;

		// This process's own: test runs at the same time never share it.
		fs::path Root() { return fs::temp_directory_path() / ("c4_file_io_test_" + std::to_string(llvm::sys::Process::getProcessId())); }

		// The spelling of an absolute path that the standard library's calls take past MAX_PATH, written out (what
		// ExtendedPath must give); Linux's take any path as it is.
#ifdef _WIN32
		fs::path LongPath(const fs::path& path) { return fs::path(L"\\\\?\\" + path.wstring()); }
#else
		fs::path LongPath(const fs::path& path) { return path; }
#endif

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

#ifdef _WIN32
	void FileIoTest::AFileInUseIsSetAsideAndReplaced()
	{
		RemoveRoot();
		fs::create_directories(Root());
		const std::string from = PathText(Root() / "C4Script.new");
		const std::string to = PathText(Root() / "C4Script.dll");
		TEST_ASSERT(WriteFileBytes(to, "old library"));
		TEST_ASSERT(WriteFileBytes(from, "new library"));
		const auto later = fs::last_write_time(Root() / "C4Script.dll") + std::chrono::seconds(5);
		fs::last_write_time(Root() / "C4Script.new", later);
		// What a loaded DLL is to others: no writes, but it may be renamed.
		HANDLE held = CreateFileW((Root() / "C4Script.dll").wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		TEST_ASSERT(held != INVALID_HANDLE_VALUE);
		TEST_ASSERT(!UpdateFile(from, to));
		TEST_ASSERT_EQ(ReadFileBytes(to), std::string("new library"));
		std::size_t aside = 0;
		for (const auto& entry : fs::directory_iterator(Root()))
			aside += entry.path().extension() == ".old" ? 1 : 0;
		TEST_ASSERT_EQ(aside, std::size_t(1)); // the one in use, kept under another name
		CloseHandle(held);
		TEST_ASSERT(!UpdateFile(from, to)); // up to date: nothing copied, and what was set aside is free now
		aside = 0;
		for (const auto& entry : fs::directory_iterator(Root()))
			aside += entry.path().extension() == ".old" ? 1 : 0;
		TEST_ASSERT_EQ(aside, std::size_t(0));
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
#else
	void FileIoTest::AFileInUseIsReplacedByANewFile()
	{
		RemoveRoot();
		fs::create_directories(Root());
		const std::string from = PathText(Root() / "C4Script.new");
		const std::string to = PathText(Root() / "C4Script.so");
		TEST_ASSERT(WriteFileBytes(to, "old library"));
		TEST_ASSERT(WriteFileBytes(from, "new library"));
		const auto later = fs::last_write_time(Root() / "C4Script.so") + std::chrono::seconds(5);
		fs::last_write_time(Root() / "C4Script.new", later);
		std::ifstream held(to, std::ios::binary); // what a program that loaded it has open
		TEST_ASSERT(held.is_open());
		TEST_ASSERT(!UpdateFile(from, to));
		TEST_ASSERT_EQ(ReadFileBytes(to), std::string("new library"));
		std::string kept((std::istreambuf_iterator<char>(held)), std::istreambuf_iterator<char>());
		TEST_ASSERT_EQ(kept, std::string("old library")); // the program's file was not written under it
		std::size_t files = 0;
		for (const auto& entry : fs::directory_iterator(Root()))
			files += entry.is_regular_file() ? 1 : 0;
		TEST_ASSERT_EQ(files, std::size_t(2)); // the source and the replaced file: no partial copy left
		TEST_ASSERT(!UpdateFile(from, to)); // again, over a file nothing holds
		TEST_ASSERT_EQ(ReadFileBytes(to), std::string("new library"));
		RemoveRoot();
	}

	void FileIoTest::ExtendedPathsAreAbsoluteAndNormal()
	{
		TEST_ASSERT(ExtendedPath("/a/b/../c//d") == fs::path("/a/c/d"));
		TEST_ASSERT(ExtendedPath("relative/x") == (fs::current_path() / "relative" / "x").lexically_normal());
		TEST_ASSERT(ExtendedPath(Root()) == Root().lexically_normal());
		TEST_ASSERT(PlainPath("/a/c") == fs::path("/a/c"));
		TEST_ASSERT(PlainPath(ExtendedPath("/plain")) == fs::path("/plain"));
		TEST_ASSERT(PathText(fs::path("/caf\xc3\xa9")) == "/caf\xc3\xa9");
	}
#endif
} // namespace Testing
