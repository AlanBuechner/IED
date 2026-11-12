// The catalog of what a sandbox can be made from (docs/34 "The catalog"): operating systems, their versions with the
// image and platforms of each, and presets - named sets of packages - per package manager. The plugin's catalog.json,
// then the owner's sandbox.images entries (an entry of a shipped id replaces it).

include "bedrock.ch"

namespace Sandbox
{
	class CatalogVersion
	{
		public Core::String m_Version;                  // "24.04"
		public Core::String m_Title;                    // "24.04 LTS (Noble)"
		public Core::String m_Image;                    // "ubuntu:24.04"
		public bool m_Default;
		public Core::Array<Core::String> m_Platforms;   // "linux/amd64", "linux/arm64", ...
	}

	class CatalogOs
	{
		public Core::String m_Id;                       // "ubuntu"
		public Core::String m_Title;
		public Core::String m_Family;                   // "debian", "fedora", "alpine", "arch", "suse", "rhel", "windows"
		public Core::String m_PackageManager;           // "apt", "dnf", "apk", "pacman", "zypper"; "" for none
		public Core::String m_Engine;                   // "linux" or "windows"
		public Core::String m_Note;
		public bool m_Owner;                            // from sandbox.images
		public Core::Array<CatalogVersion> m_Versions;
	}

	class PresetPackages
	{
		public Core::String m_Manager;
		public Core::Array<Core::String> m_Names;
	}

	class CatalogPreset
	{
		public Core::String m_Id;
		public Core::String m_Title;
		public Core::Array<PresetPackages> m_Packages;
	}

	class Catalog
	{
		public Core::Array<CatalogOs> m_Systems;
		public Core::Array<CatalogPreset> m_Presets;
	}

	// The catalog in `json` (catalog.json's shape: {systems, presets}); false with `problem`.
	bool ParseCatalog(Core::StringView json, Catalog& catalog, Core::String& problem);
	// Adds the owner's systems (sandbox.images: an array of catalog systems), each replacing a shipped one of its id.
	void AddOwnerSystems(Catalog& catalog, const Serialization::DomNode& systems);
	// The system `id`; null when there is none.
	const CatalogOs* FindSystem(const Catalog& catalog, Core::StringView id);
	const CatalogPreset* FindPreset(const Catalog& catalog, Core::StringView id);
	// The packages `preset` installs with `manager`; false when the preset has none for it.
	bool PresetNames(const CatalogPreset& preset, Core::StringView manager, Core::Array<Core::String>& names);
	// "ubuntu:24.04" (or "ubuntu" for its default version) as a system and version of the catalog; false with `problem`
	// naming what there is.
	bool ResolveOs(const Catalog& catalog, Core::StringView spec, const CatalogOs*& os, const CatalogVersion*& version, Core::String& problem);
	// A platform as Docker names it, from what a person or `docker` writes: "arm64", "aarch64", "linux/arm64/v8" ->
	// "linux/arm64"; "x86_64", "amd64" -> "linux/amd64"; "armv7", "arm" -> "linux/arm/v7"; "i386", "x86" -> "linux/386".
	// "" stays "".
	Core::String NormalPlatform(Core::StringView platform);
	// Whether `platforms` (normal) holds `platform` (normal).
	bool HasPlatform(const Core::Array<Core::String>& platforms, Core::StringView platform);
}
