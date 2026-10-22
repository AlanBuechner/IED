#include "Script/InlineBodyCache.h"
#include "Compiler/FileIo.h"
#include "Compiler/ObjectCache.h"
#include "Compiler/ParallelFor.h"
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/xxhash.h>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace Scripting
{
	namespace
	{
		constexpr char kMagic[] = "C4IB";
		constexpr size_t kMagicBytes = sizeof(kMagic) - 1;

		std::string Hex(uint64_t value)
		{
			char text[24];
			snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
			return text;
		}

		// The chunks as one file: a mark, then each chunk's size (8 bytes, as the machine orders them) and bytes.
		std::string Packed(const std::vector<std::string>& chunks)
		{
			std::string bytes(kMagic, kMagicBytes);
			for (const std::string& chunk : chunks)
			{
				const uint64_t size = chunk.size();
				bytes.append(reinterpret_cast<const char*>(&size), sizeof(size));
				bytes += chunk;
			}
			return bytes;
		}

		// The chunks of a file Packed wrote; none for a file that is missing, unreadable or cut short.
		std::optional<std::vector<std::string>> Unpacked(const std::string& bytes)
		{
			if (bytes.compare(0, kMagicBytes, kMagic) != 0) return std::nullopt;
			std::vector<std::string> chunks;
			for (size_t at = kMagicBytes; at < bytes.size();)
			{
				uint64_t size = 0;
				if (bytes.size() - at < sizeof(size)) return std::nullopt;
				std::memcpy(&size, bytes.data() + at, sizeof(size));
				at += sizeof(size);
				if (bytes.size() - at < size) return std::nullopt;
				chunks.push_back(bytes.substr(at, size));
				at += size;
			}
			return chunks;
		}

		// Written beside its name and moved there once complete, as an object is.
		bool Kept(const std::string& path, const std::vector<std::string>& chunks)
		{
			const std::string unfinished = path + ".tmp";
			if (WriteFileBytes(unfinished, Packed(chunks)) && !RenameFile(unfinished, path)) return true;
			RemoveFile(unfinished);
			return false;
		}
	}

	std::optional<std::vector<std::vector<std::string>>> InlineBodyCache::Chunks(const std::vector<llvm::Module*>& partitions,
		const std::vector<std::string>& bitcode, const HostNames& names, unsigned maxGroups, unsigned threads, std::string& error) const
	{
		if (CreateFolders(PathText(m_Folder)))
		{
			error = "cannot create the inline body cache '" + PathText(m_Folder) + "'";
			return std::nullopt;
		}
		const std::string salt = Hex(llvm::xxh3_64bits(llvm::StringRef(m_Salt)));
		std::vector<std::filesystem::path> paths(partitions.size());
		std::unordered_map<std::string, size_t> first; // partitions with the same code share a file (BUG-366)
		std::vector<size_t> unique;
		for (size_t i = 0; i < partitions.size(); ++i)
		{
			const llvm::XXH128_hash_t code = llvm::xxh3_128bits(llvm::arrayRefFromStringRef(bitcode[i]));
			paths[i] = m_Folder / (Hex(code.high64) + Hex(code.low64) + "-" + salt + ".inline");
			if (first.try_emplace(PathText(paths[i]), i).second) unique.push_back(i);
		}
		std::vector<std::vector<std::string>> chunks(partitions.size());
		std::vector<std::string> failures(unique.size());
		ParallelFor(unique.size(), threads, [&](size_t k) {
			const size_t i = unique[k];
			const std::string path = PathText(paths[i]);
			if (std::optional<std::vector<std::string>> read = Unpacked(ReadFileBytes(path)))
			{
				chunks[i] = std::move(*read);
				return;
			}
			chunks[i] = InlineBodies(*partitions[i], names, maxGroups);
			if (!Kept(path, chunks[i])) failures[k] = "cannot write the inline bodies '" + path + "'";
		});
		for (const std::string& failure : failures)
			if (!failure.empty())
			{
				error = failure;
				return std::nullopt;
			}
		for (size_t i = 0; i < partitions.size(); ++i)
			if (const size_t owner = first.at(PathText(paths[i])); owner != i) chunks[i] = chunks[owner];
		ObjectCache(m_Folder, m_Salt).KeepOnly(paths);
		return chunks;
	}

	std::string HostNamesSalt(const std::vector<std::string>& names)
	{
		std::string joined;
		for (const std::string& name : names) joined.append(name).push_back('\0');
		return Hex(llvm::xxh3_64bits(llvm::StringRef(joined)));
	}
} // namespace Scripting
