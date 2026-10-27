// Installing a language tool (docs/26 "Language tools"): its steps for this system, in order, into its folder under IED's
// tools folder (<data>/tools/<id>/<version>). A download is fetched with the system's curl, checked against its SHA-256
// (the manifest's for the tool's install version, else the digest file the step names; neither: refused) and unpacked
// with the system's tar; a command runs with the tools PATH. A step that fails stops the install and its folder goes.
// Runs on the install's thread without the server lock; what it does goes to `say`.

include "Spec.ch"

namespace Ied
{
	alias InstallSay = func<void(Core::StringView text)>;
	// A step's string with its variables expanded (${version} and ${installDir} too); called before each step runs, so a
	// tool installed earlier in the same run is what ${tool:<id>} names.
	alias StepExpander = func<Core::String(Core::StringView text)>;

	// The command an install runs now, so a cancel ends it.
	class InstallControl
	{
		public void Cancel();
		public bool Cancelled();
		// Keeps `child` as the running command; false (and it is killed) when the install was cancelled already.
		public bool Running(Process::Child* child);
		public void Ended();

		private Threading::Mutex m_Lock;
		private Process::Child* m_Child;
		private bool m_Cancelled;
	}

	class InstallPlan
	{
		public ToolSpec* m_Spec;
		public Core::String m_Version;
		public FS::Path m_Folder;                        // <data>/tools/<id>/<version>
		public FS::Path m_Downloads;                     // <data>/tools/.downloads
		public Core::String m_Cwd;                       // a command's folder when its step names none: the workspace's
		public Core::Array<Core::String> m_Environment;  // every command's ("NAME=value": the tools PATH)
		public StepExpander m_Expand;
	}

	// Runs `plan`'s steps; false with `problem` at the first that fails or when cancelled (the install folder removed).
	bool RunInstall(const InstallPlan& plan, InstallControl* control, const InstallSay& say, Core::String& problem);
	// The first word of 64 hexadecimal digits in `text` (a .sha256 file, "sha256:<hex>"), in lower case; empty without
	// one.
	Core::String DigestIn(Core::StringView text);
	// The SHA-256 of a file in lower-case hexadecimal, read in pieces; empty when it cannot be read.
	Core::String FileDigest(const FS::Path& file);
	// `text` with every `from` replaced by `to`.
	Core::String ReplaceAll(Core::StringView text, Core::StringView from, Core::StringView to);
	// The name a URL's path ends with ("rustup-init.exe"); "download" when it has none.
	Core::String UrlFileName(Core::StringView url);
}
