// A sandbox's definition (docs/34 "Definitions"): .ied/sandbox/<name>/sandbox.json, committed; and what is made of it -
// the Dockerfile IED builds, the image's tag, the container's name and the docker arguments that start it and run in it.
// Everything here is text in, text out: the service runs it.

include "SandboxCatalog.ch"

namespace Sandbox
{
	class SandboxDefinition
	{
		public Core::String m_Name;
		public Core::String m_Os;                       // "ubuntu:24.04"; "" when only `image` names it
		public Core::String m_Image;                    // the image as written; "" to take the catalog's
		public Core::String m_Platform;                 // normal ("linux/arm64"); "" for the engine's own
		public Core::Array<Core::String> m_Presets;
		public Core::Array<Core::String> m_Packages;
		public Core::Array<Core::String> m_Setup;       // shell lines, run as root when the image is built
		public Core::Array<Core::String> m_Env;         // "NAME=value"
		public Core::String m_Validate;
		public Core::String m_Network;                  // "none" or "full"
		public int64 m_Cpus;                            // 0: the setting's
		public int64 m_MemoryGiB;                       // 0: the setting's
		public Core::String m_Description;
		public bool m_OwnDockerfile;                    // .ied/sandbox/<name>/Dockerfile exists: it is built as it is
		public Core::String m_OwnDockerfileText;        // its text, for the image's digest
		public bool m_Desktop;                          // a virtual screen with a window manager, DISPLAY for every run
		public int64 m_DesktopWidth;                    // its size; 0: c_DesktopWidth x c_DesktopHeight
		public int64 m_DesktopHeight;
	}

	// What a definition becomes once the catalog and the settings are applied.
	class SandboxPlan
	{
		public Core::String m_BaseImage;                // the FROM image, the mirror applied
		public Core::String m_Family;                   // the catalog's, "" for an image the catalog does not know
		public Core::String m_PackageManager;
		public Core::String m_Engine;                   // "linux" or "windows"
		public Core::String m_Dockerfile;               // the text IED builds; "" for the owner's own Dockerfile
		public Core::Array<Core::String> m_Missing;     // presets the package manager has no packages for
		public Core::String m_Digest;                   // 12 hex digits over the Dockerfile and the platform
		public Core::String m_Tag;                      // ied-sandbox/<key>-<name>:<digest>
		public Core::String m_Container;                // ied-<key>-<name>
		public Core::String m_RunDigest;                // 12 hex digits over the image and the run options
		public int64 m_Cpus;
		public int64 m_MemoryGiB;
	}

	// Whether `name` can name a sandbox: lowercase letters, digits and '-', 1 to 40, starting with a letter or digit.
	bool ValidName(Core::StringView name);
	// The definition in `json` (sandbox.json's text) of sandbox `name`; false with `problem` (a parse error, a wrong
	// field). Defaults filled in: network "none".
	bool ParseDefinition(Core::StringView name, Core::StringView json, SandboxDefinition& definition, Core::String& problem);
	// sandbox.json's text for `definition`, tab-indented, the fields it sets only.
	Core::String DefinitionJson(const SandboxDefinition& definition);

	// The plan of `definition` in a workspace whose key is `key`, its root `root`: the catalog's image and packages, the
	// mirror for official images (sandbox.registryMirror), the settings' limits. False with `problem` when the
	// definition names no system the catalog has and no image.
	bool PlanSandbox(const SandboxDefinition& definition, const Catalog& catalog, Core::StringView key, Core::StringView root, Core::StringView mirror, int64 cpus, int64 memoryGiB, SandboxPlan& plan, Core::String& problem);
	// The command a package manager installs `packages` with, non-interactively, cleaning its caches after.
	Core::String InstallCommand(Core::StringView manager, const Core::Array<Core::String>& packages);
	// `image` from the mirror: an official image ("ubuntu:24.04", "library/ubuntu") as "<mirror>/ubuntu:24.04"; any
	// other (a namespace or a registry) as it is. An empty mirror changes nothing.
	Core::String MirrorImage(Core::StringView image, Core::StringView mirror);
	// 8 hex digits naming a workspace root (its path with '/' and lowercase), so two checkouts never share a container.
	Core::String WorkspaceKey(Core::StringView root);
	// The first `digits` hex digits of SHA-256 over `text`.
	Core::String ShortDigest(Core::StringView text, int64 digits);

	// `docker run` arguments (after the docker program) that start the plan's container for workspace `root`.
	Core::Array<Core::String> RunArguments(const SandboxDefinition& definition, const SandboxPlan& plan, Core::StringView root);
	// `docker exec` arguments that run `command` (a shell line) in `container`, in /workspace/<cwd>, with `env`
	// ("NAME=value") - its shell's process id written to `pidFile` inside the container, so a stop can end it and
	// what it started (a docker exec is the leader of its own process group).
	Core::Array<Core::String> ExecArguments(Core::StringView container, Core::StringView cwd, const Core::Array<Core::String>& env, Core::StringView command, Core::StringView pidFile);
	// `docker exec` arguments that end the run whose shell wrote `pidFile`: its process group, then the shell itself.
	Core::Array<Core::String> KillArguments(Core::StringView container, Core::StringView pidFile);
	// A workspace-relative folder as a path under /workspace ("" is /workspace itself).
	Core::String ContainerPath(Core::StringView cwd);

	const char* c_DefinitionsFolder = ".ied/sandbox";
	const int64 c_DesktopWidth = 1280;
	const int64 c_DesktopHeight = 800;
	// The preset a desktop installs: the virtual screen, a window manager, xdotool, ImageMagick, fonts, Mesa.
	const char* c_DesktopPreset = "desktop";
	const char* c_WorkspaceMount = "/workspace";
}
