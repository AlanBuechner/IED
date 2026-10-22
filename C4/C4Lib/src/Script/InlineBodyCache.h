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
	/// chunks (InlineBodies) are named after the hash of its bitcode and of `salt` (the compiler, the target and the
	/// names every table lists, HostNamesSalt), so a partition whose code is unchanged reads them back instead of
	/// extracting them. Its bodies name only what it lists itself (HostEntries) and those. The folder keeps only the
	/// latest build's.
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

	/// What the names every table lists beside the runtime's (HostEntries' `alsoListed`) add to an InlineBodyCache's
	/// salt: a body may name them whether or not its partition does.
	std::string HostNamesSalt(const std::vector<std::string>& names);
} // namespace Scripting
