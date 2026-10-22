#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>

namespace Scripting
{
	// Files by path through LLVM's calls, which take paths longer than MAX_PATH (a deep build or project folder);
	// std::filesystem's and the standard streams' do not.

	/// The file's bytes; empty when it cannot be read.
	std::string ReadFileBytes(const std::string& path);
	/// `bytes` as the whole file; false when it cannot be written.
	bool WriteFileBytes(const std::string& path, const std::string& bytes);
	bool FileExists(const std::string& path);
	void RemoveFile(const std::string& path);
	/// The folder and any missing parents.
	std::error_code CreateFolders(const std::string& path);
	/// `from` renamed to `to`, replacing a file there.
	std::error_code RenameFile(const std::string& from, const std::string& to);
	/// `from` copied over `to` unless `to` is at least as new.
	std::error_code UpdateFile(const std::string& from, const std::string& to);

	struct FileStamp
	{
		uint64_t size = 0;
		int64_t modified = 0; // nanoseconds since the epoch
	};

	/// The file's size and modification time; none when there is no such file.
	std::optional<FileStamp> StampOf(const std::string& path);
}
