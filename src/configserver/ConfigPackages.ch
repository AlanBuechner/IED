// ConfigPackages (docs/35): a package a config server serves - a plugin's folder, a skill's - as files with their sizes
// and SHA-256s, and the package's hash: one digest of every file's path, size and SHA-256, in path order. The server
// and the clients that mirror it compute it alike, so a client knows when its copy is whole and current.

include "bedrock.ch"

namespace Ied
{
	class PackageFile
	{
		public Core::String m_Path;   // relative to the package, '/' separators
		public int64 m_Size;
		public Core::String m_Hash;   // SHA-256 of its bytes, hex
	}

	// The files under `folder` (links left out), in path order; none for a folder that is not there.
	Core::Array<PackageFile*> ListPackage(const FS::Path& folder);
	// The package's hash of `files` (in path order).
	Core::String PackageHash(const Core::Array<PackageFile*>& files);
	// [{path, size, hash}]
	Core::String PackageFilesJson(const Core::Array<PackageFile*>& files);
	// A package's id or a skill's name: letters, digits, '.', '_' and '-', not starting with '.', at most 128 bytes.
	bool IsPackageName(Core::StringView name);
	// A file's path in a package as a client may name it: relative, '/' separators, each part a plain name (no '.', '..',
	// ':' or control characters; not ".staging" first), at most 1024 bytes. False with `why`.
	bool IsPackagePath(Core::StringView path, Core::String& why);
	// `path` with each byte a URL path may not carry as it is percent-escaped ('/' kept).
	Core::String EscapeUrlPath(Core::StringView path);
	// The SHA-256 of `bytes`, hex.
	Core::String BytesHash(Core::StringView bytes);
	// Puts the folder `staging` in place of `target`: what is there moved to `aside` first and removed once the new one is
	// in place (put back when it cannot be), so a reader never sees half a package. False with `why`.
	bool SwapFolder(const FS::Path& staging, const FS::Path& target, const FS::Path& aside, Core::String& why);

	// The folder a package is staged in while it is deployed or mirrored, beside its kind's packages.
	const char* c_StagingFolder = ".staging";
}
