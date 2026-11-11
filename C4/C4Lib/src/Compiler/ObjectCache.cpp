#include "Compiler/ObjectCache.h"
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/xxhash.h>
#include <atomic>
#include <cstdio>
#include <set>
#include <system_error>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Scripting
{
	namespace
	{
		std::string Hex(uint64_t value)
		{
			char text[24];
			snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
			return text;
		}

		const char* const kPartialSuffix = ".tmp";

		bool IsAlive(unsigned long pid)
		{
			HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
			if (process == nullptr) return GetLastError() == ERROR_ACCESS_DENIED; // there, but not ours to open
			const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
			CloseHandle(process);
			return running;
		}

		const char* const kClaimPrefix = ".claim.";

		unsigned long OwnProcess() { return static_cast<unsigned long>(llvm::sys::Process::getProcessId()); }

		// The process whose claims the file `name` holds, or 0 when it holds none.
		unsigned long ClaimantOf(llvm::StringRef name)
		{
			if (!name.consume_front(kClaimPrefix)) return 0;
			unsigned long pid = 0;
			return name.getAsInteger(10, pid) ? 0 : pid;
		}

		// The objects other running processes claimed in `folder`; the claims of ended ones are removed.
		std::set<std::string> OthersClaims(const std::string& folder)
		{
			std::set<std::string> claimed;
			std::error_code ec;
			for (llvm::sys::fs::directory_iterator entry(folder, ec), end; !ec && entry != end; entry.increment(ec))
			{
				const std::string path = entry->path();
				const unsigned long claimant = ClaimantOf(llvm::sys::path::filename(path));
				if (claimant == 0 || claimant == OwnProcess()) continue;
				if (!IsAlive(claimant))
				{
					(void)llvm::sys::fs::remove(path);
					continue;
				}
				llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> text = llvm::MemoryBuffer::getFile(path);
				if (!text) continue;
				llvm::SmallVector<llvm::StringRef, 64> names;
				(*text)->getBuffer().split(names, '\n', -1, false);
				for (llvm::StringRef name : names) claimed.insert(name.trim().str());
			}
			return claimed;
		}
	}

	std::filesystem::path ObjectCache::ObjectFor(const std::string& bitcode) const
	{
		const llvm::XXH128_hash_t code = llvm::xxh3_128bits(llvm::arrayRefFromStringRef(bitcode));
		return ObjectKeyed(Hex(code.high64) + Hex(code.low64));
	}

	std::filesystem::path ObjectCache::ObjectKeyed(const std::string& key) const
	{
		return m_Folder / (key + "-" + Hex(llvm::xxh3_64bits(llvm::StringRef(m_Salt))) + ".obj");
	}

	// Appends to this process's claims; one thread claims at a time (a build's partitions claim under its lock).
	void ObjectCache::Claim(const std::filesystem::path& object) const
	{
		std::error_code ec;
		llvm::raw_fd_ostream claims((m_Folder / (kClaimPrefix + std::to_string(OwnProcess()))).string(), ec,
			llvm::sys::fs::OF_Append | llvm::sys::fs::OF_Text);
		if (!ec) claims << object.filename().string() << "\n"; // unclaimed, an object is at worst assembled again
	}

	// LLVM's file calls take paths longer than MAX_PATH (a deep build folder), std::filesystem's do not.
	void ObjectCache::KeepOnly(const std::vector<std::filesystem::path>& used) const
	{
		std::set<std::filesystem::path> keep(used.begin(), used.end());
		const std::set<std::string> claimed = OthersClaims(m_Folder.string());
		std::error_code ec;
		for (llvm::sys::fs::directory_iterator entry(m_Folder.string(), ec), end; !ec && entry != end; entry.increment(ec))
		{
			const std::string path = entry->path();
			if (entry->type() != llvm::sys::fs::file_type::regular_file || keep.count(std::filesystem::path(path))) continue;
			const llvm::StringRef name = llvm::sys::path::filename(path);
			if (ClaimantOf(name) != 0 || claimed.count(name.str())) continue; // a running build's claims, or what it links
			if (IsAnotherBuildsPartial(path)) continue;
			(void)llvm::sys::fs::remove(path); // a file another program holds open stays: a later build prunes it
		}
	}

	std::string PrivatePartialPath(const std::string& path)
	{
		static std::atomic<unsigned long long> s_Written = 0;
		return path + "." + std::to_string(llvm::sys::Process::getProcessId()) + "-" + std::to_string(s_Written++) + kPartialSuffix;
	}

	bool IsAnotherBuildsPartial(const std::string& path)
	{
		// <object>.<pid>-<n>.tmp: the pid is the second dot-separated part from the end.
		llvm::StringRef name = llvm::sys::path::filename(path);
		if (!name.consume_back(kPartialSuffix)) return false;
		const size_t dot = name.rfind('.');
		if (dot == llvm::StringRef::npos) return false;
		llvm::StringRef writer = name.substr(dot + 1).split('-').first;
		unsigned long pid = 0;
		if (writer.getAsInteger(10, pid)) return false; // an older compiler's <object>.tmp: nobody's now
		return pid != static_cast<unsigned long>(llvm::sys::Process::getProcessId()) && IsAlive(pid);
	}
}
