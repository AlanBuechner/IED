#include "Compiler/ObjectCache.h"
#include <llvm/ADT/StringExtras.h>
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

	void ObjectCache::KeepOnly(const std::vector<std::filesystem::path>& used) const
	{
		std::set<std::filesystem::path> keep(used.begin(), used.end());
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(m_Folder, ec))
			if (entry.is_regular_file(ec) && !keep.count(entry.path())) std::filesystem::remove(entry.path(), ec);
	}
}
