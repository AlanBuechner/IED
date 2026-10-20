#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Scripting
{
	/// Assembled partitions kept between builds by what produced them (roadmap 5.3): an object is named after the
	/// hash of its partition's bitcode and of `salt` (the compiler and the target settings), so a partition whose
	/// code, settings and compiler are unchanged reuses its object instead of being assembled again.
	class ObjectCache
	{
	public:
		ObjectCache(std::filesystem::path folder, std::string salt) : m_Folder(std::move(folder)), m_Salt(std::move(salt)) {}

		const std::filesystem::path& Folder() const { return m_Folder; }

		/// Where the object of `bitcode` is kept, whether or not it is there yet.
		std::filesystem::path ObjectFor(const std::string& bitcode) const;

		/// Removes every object (and unfinished write) but `used`: the folder holds one build's objects.
		void KeepOnly(const std::vector<std::filesystem::path>& used) const;

	private:
		std::filesystem::path m_Folder;
		std::string m_Salt;
	};
}
