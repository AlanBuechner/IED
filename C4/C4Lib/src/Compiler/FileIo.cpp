#include "Compiler/FileIo.h"
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/raw_ostream.h>
#include <chrono>

namespace Scripting
{
	std::string ReadFileBytes(const std::string& path)
	{
		llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> file = llvm::MemoryBuffer::getFile(path, /*IsText=*/false,
			/*RequiresNullTerminator=*/false);
		return file ? (*file)->getBuffer().str() : std::string();
	}

	bool WriteFileBytes(const std::string& path, const std::string& bytes)
	{
		std::error_code ec;
		llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_None);
		if (ec) return false;
		out << bytes;
		out.close();
		return !out.has_error();
	}

	std::string PathText(const std::filesystem::path& path)
	{
		const std::u8string text = path.u8string();
		return std::string(text.begin(), text.end());
	}

	bool FileExists(const std::string& path) { return llvm::sys::fs::exists(path); }

	bool IsFile(const std::string& path) { return llvm::sys::fs::is_regular_file(path); }

	bool IsFolder(const std::string& path) { return llvm::sys::fs::is_directory(path); }

#ifdef _WIN32
	namespace
	{
		const std::wstring kExtended = L"\\\\?\\";
		const std::wstring kExtendedShare = L"\\\\?\\UNC\\";
		const std::wstring kShare = L"\\\\";
	}

	std::filesystem::path ExtendedPath(const std::filesystem::path& path)
	{
		if (path.wstring().rfind(kExtended, 0) == 0) return path;
		std::error_code ec;
		const std::filesystem::path full = std::filesystem::absolute(path, ec);
		const std::wstring normal = (ec ? path : full).lexically_normal().make_preferred().wstring();
		if (normal.rfind(kShare, 0) == 0) return std::filesystem::path(kExtendedShare + normal.substr(kShare.size()));
		return std::filesystem::path(kExtended + normal);
	}

	std::filesystem::path PlainPath(const std::filesystem::path& path)
	{
		const std::wstring text = path.wstring();
		if (text.rfind(kExtendedShare, 0) == 0) return std::filesystem::path(kShare + text.substr(kExtendedShare.size()));
		if (text.rfind(kExtended, 0) == 0) return std::filesystem::path(text.substr(kExtended.size()));
		return path;
	}
#else
	// Linux has no path length limit to spell around: a path is only made absolute and normal, as on Windows.
	std::filesystem::path ExtendedPath(const std::filesystem::path& path)
	{
		std::error_code ec;
		const std::filesystem::path full = std::filesystem::absolute(path, ec);
		return (ec ? path : full).lexically_normal();
	}

	std::filesystem::path PlainPath(const std::filesystem::path& path) { return path; }
#endif

	void RemoveFile(const std::string& path) { llvm::sys::fs::remove(path); }

	std::error_code CreateFolders(const std::string& path) { return llvm::sys::fs::create_directories(path); }

	std::error_code RenameFile(const std::string& from, const std::string& to) { return llvm::sys::fs::rename(from, to); }

	namespace
	{
		const char* kAsideSuffix = ".old";

		// `<to>.<n>.old`: where a file in use goes so a new one can take its name.
		std::string AsidePath(const std::string& to)
		{
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
			return to + "." + std::to_string(now.count()) + kAsideSuffix;
		}

		// The copies of `to` moved aside earlier that nothing has open now.
		void RemoveSetAside(const std::string& to)
		{
			const std::filesystem::path file = std::filesystem::path(std::u8string(to.begin(), to.end()));
			const std::string prefix = PathText(file.filename()) + ".";
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(ExtendedPath(file.parent_path()), ec))
			{
				const std::string name = PathText(entry.path().filename());
				const bool aside = name.size() > prefix.size() + 4 && name.rfind(prefix, 0) == 0 && name.compare(name.size() - 4, 4, kAsideSuffix) == 0;
				if (aside) RemoveFile(PathText(PlainPath(entry.path())));
			}
		}
	}

	std::error_code UpdateFile(const std::string& from, const std::string& to)
	{
		const std::optional<FileStamp> source = StampOf(from), existing = StampOf(to);
		if (!source) return std::make_error_code(std::errc::no_such_file_or_directory);
		RemoveSetAside(to);
		if (existing && existing->modified >= source->modified) return {};
#ifdef _WIN32
		const std::error_code copied = llvm::sys::fs::copy_file(from, to);
		if (!copied || !existing) return copied;
		// A running program has it loaded (a DLL beside it): Windows will not write it, but renames it.
		if (RenameFile(to, AsidePath(to))) return copied;
		return llvm::sys::fs::copy_file(from, to);
#else
		// A running program may have `to` mapped (a shared library beside it): Linux would write the code under it, so the
		// copy goes beside it and is renamed over it, and the program keeps the file it has open.
		const std::string partial = to + "." + std::to_string(llvm::sys::Process::getProcessId()) + ".partial";
		if (const std::error_code copied = llvm::sys::fs::copy_file(from, partial)) return copied;
		const std::error_code renamed = RenameFile(partial, to);
		if (renamed) RemoveFile(partial);
		return renamed;
#endif
	}

	std::optional<FileStamp> StampOf(const std::string& path)
	{
		llvm::sys::fs::file_status status;
		if (llvm::sys::fs::status(path, status) || !llvm::sys::fs::exists(status)) return std::nullopt;
		const auto modified = std::chrono::duration_cast<std::chrono::nanoseconds>(status.getLastModificationTime().time_since_epoch());
		return FileStamp{ status.getSize(), modified.count() };
	}
}
