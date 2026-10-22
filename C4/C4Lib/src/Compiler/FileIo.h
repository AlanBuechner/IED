#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace Scripting
{
	// Files by path through LLVM's calls, which take paths longer than MAX_PATH (a deep build or project folder);
	// std::filesystem's and the standard streams' do not. Paths are UTF-8.

	/// `path` in UTF-8, as these calls take it (path::string() is the ANSI code page on Windows).
	std::string PathText(const std::filesystem::path& path);
	/// `path` as std::filesystem's calls take it past MAX_PATH: absolute and normal, spelled `\\?\` (`\\?\UNC\` for a
	/// share). For what LLVM has no call for: walking a folder, or removing one (std::filesystem never follows a
	/// junction there; LLVM's walk takes a junction for a folder).
	std::filesystem::path ExtendedPath(const std::filesystem::path& path);
	/// A path ExtendedPath spelled, or an entry of a walk of one, as it reads without the spelling.
	std::filesystem::path PlainPath(const std::filesystem::path& path);

	/// The file's bytes; empty when it cannot be read.
	std::string ReadFileBytes(const std::string& path);
	/// `bytes` as the whole file; false when it cannot be written.
	bool WriteFileBytes(const std::string& path, const std::string& bytes);
	/// Whether anything (a file or a folder) is at `path`.
	bool FileExists(const std::string& path);
	/// Whether a regular file, or a folder, is at `path` (through a link).
	bool IsFile(const std::string& path);
	bool IsFolder(const std::string& path);
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
