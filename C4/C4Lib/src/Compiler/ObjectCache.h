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
		/// Where the object of a module is kept by `key`, which says everything the module is made of.
		std::filesystem::path ObjectKeyed(const std::string& key) const;

		/// Notes that this process's build links `object` (a file of the folder, there yet or not): another build's KeepOnly
		/// spares it while this process runs (BUG-443). The claims are a file of the folder per process, `.claim.<pid>`.
		void Claim(const std::filesystem::path& object) const;

		/// Removes every object (and unfinished write) but `used`: the folder holds one build's objects. What another
		/// compiler still running claimed (two IED chats building one project at once, BUG-443) stays, and so does its
		/// unfinished write (IsAnotherBuildsPartial), so its rename into place does not fail (BUG-442); an ended one's
		/// claims go.
		void KeepOnly(const std::vector<std::filesystem::path>& used) const;

	private:
		std::filesystem::path m_Folder;
		std::string m_Salt;
	};

	/// A name beside `path` no other process or thread writes (`<path>.<pid>-<n>.tmp`): where a file is written before
	/// it is renamed to `path`, so two compilers making the same object never take each other's unfinished file (BUG-442).
	std::string PrivatePartialPath(const std::string& path);
	/// Whether `path` is a PrivatePartialPath of another process that is still running: a write KeepOnly leaves alone.
	bool IsAnotherBuildsPartial(const std::string& path);
}
