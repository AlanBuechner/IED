#pragma once
#include "Script/InlineBodies.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// A split program's inline bodies kept between builds in a folder of their own (docs/compiler/05): a partition's
	/// chunks (InlineBodies) are named after the hash of its bitcode and of `salt` (the compiler, the target and the host
	/// table's names, HostNamesSalt), so a partition whose code and table are unchanged reads them back instead of
	/// extracting them. The folder keeps only the latest build's.
	class InlineBodyCache
	{
	public:
		InlineBodyCache(std::filesystem::path folder, std::string salt) : m_Folder(std::move(folder)), m_Salt(std::move(salt)) {}

		/// Each partition's chunks, up to `maxGroups` each, in partition order: a kept one's read back, the others'
		/// extracted from `partitions[i]`, whose bitcode is `bitcode[i]`, on up to `threads` threads and kept. None, with
		/// the reason in `error`, when the folder cannot be made or a file written.
		std::optional<std::vector<std::vector<std::string>>> Chunks(const std::vector<llvm::Module*>& partitions,
			const std::vector<std::string>& bitcode, const HostNames& names, unsigned maxGroups, unsigned threads, std::string& error) const;

	private:
		std::filesystem::path m_Folder;
		std::string m_Salt;
	};

	/// What a host table's names add to an InlineBodyCache's salt: which functions a body may name.
	std::string HostNamesSalt(const std::vector<std::string>& symbols);
} // namespace Scripting
