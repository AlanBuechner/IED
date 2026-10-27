// A task preset no loaded plugin offers (docs/06 "Presets"): the problem names the workspace's plugin that offers it
// but is not loaded, and why - a manifest the schema refuses, a workspace not trusted, a plugin turned off or waiting
// for approval - so a broken language plugin does not show only as a missing preset (C4 BUG-385: the c4 plugin's
// manifest failed its schema, and C4 projects said "no plugin offers the task preset c4.nuke").

include "plugins/Host.ch"

namespace Ied
{
	// Words a problem for a preset no loaded plugin offers.
	alias MissingPreset = func<Core::String(Core::StringView id)>;

	// "no plugin offers the task preset <id>", and when one of `plugins` (the workspace's, its own first) offers it but
	// is not loaded, which one and why. A plugin that is not loaded and whose manifest could not be read may be the one,
	// so it is named too.
	Core::String MissingPresetText(Core::StringView id, const Core::Array<Plugin*>& plugins);
}
