#include "Compiler/FileIo.h"
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
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

	bool FileExists(const std::string& path) { return llvm::sys::fs::exists(path); }

	void RemoveFile(const std::string& path) { llvm::sys::fs::remove(path); }

	std::error_code CreateFolders(const std::string& path) { return llvm::sys::fs::create_directories(path); }

	std::error_code RenameFile(const std::string& from, const std::string& to) { return llvm::sys::fs::rename(from, to); }

	std::error_code UpdateFile(const std::string& from, const std::string& to)
	{
		const std::optional<FileStamp> source = StampOf(from), existing = StampOf(to);
		if (!source) return std::make_error_code(std::errc::no_such_file_or_directory);
		if (existing && existing->modified >= source->modified) return {};
		return llvm::sys::fs::copy_file(from, to);
	}

	std::optional<FileStamp> StampOf(const std::string& path)
	{
		llvm::sys::fs::file_status status;
		if (llvm::sys::fs::status(path, status) || !llvm::sys::fs::exists(status)) return std::nullopt;
		const auto modified = std::chrono::duration_cast<std::chrono::nanoseconds>(status.getLastModificationTime().time_since_epoch());
		return FileStamp{ status.getSize(), modified.count() };
	}
}
