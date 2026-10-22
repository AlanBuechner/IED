#include "Compiler/ObjectCache.h"
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/xxhash.h>
#include <cstdio>
#include <set>
#include <system_error>

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
	}

	std::filesystem::path ObjectCache::ObjectFor(const std::string& bitcode) const
	{
		const llvm::XXH128_hash_t code = llvm::xxh3_128bits(llvm::arrayRefFromStringRef(bitcode));
		return m_Folder / (Hex(code.high64) + Hex(code.low64) + "-" + Hex(llvm::xxh3_64bits(llvm::StringRef(m_Salt))) + ".obj");
	}

	// LLVM's file calls take paths longer than MAX_PATH (a deep build folder), std::filesystem's do not.
	void ObjectCache::KeepOnly(const std::vector<std::filesystem::path>& used) const
	{
		std::set<std::filesystem::path> keep(used.begin(), used.end());
		std::error_code ec;
		for (llvm::sys::fs::directory_iterator entry(m_Folder.string(), ec), end; !ec && entry != end; entry.increment(ec))
			if (entry->type() == llvm::sys::fs::file_type::regular_file && !keep.count(std::filesystem::path(entry->path())))
				llvm::sys::fs::remove(entry->path());
	}
}
