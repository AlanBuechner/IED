// IED's own solution and project files, for a solution no build system generates (docs/03 "Managing projects"): a new
// file's text, and a solution file's `projects` and `groups` changed in place with its comments and other members kept
// (settings/JsonEdit.ch). Paths are relative to the solution's folder, with '/' separators.

include "solutions/Model.ch"

namespace Ied
{
	// {version, name, root, kind, language?, presets?}
	Core::String NewProjectFileText(Core::StringView name, Core::StringView root, Core::StringView kind, Core::StringView language, const Core::Array<Core::String>& presets);
	// {version, name, projects: [{file} for each of `files`]}
	Core::String NewSolutionFileText(Core::StringView name, const Core::Array<Core::String>& files);
	// The solution file's text with `file` added to its projects.
	bool ListProjectFile(Core::StringView text, Core::StringView file, Core::String& edited, Core::String& error);
	// The solution file's text without the projects entries naming `file` and without `name` in its groups.
	bool UnlistProject(Core::StringView text, Core::StringView file, Core::StringView name, Core::String& edited, Core::String& error);
	// The solution file's text with the projects entry naming `from` naming `to` instead.
	bool RelistProjectFile(Core::StringView text, Core::StringView from, Core::StringView to, Core::String& edited, Core::String& error);
	// Whether an entry of the solution file's `projects` names `file`: exactly, or by a glob that matches it.
	bool ListsProjectFile(Core::StringView text, Core::StringView file);
	// The way from the folder `from` to `to`: "../libs/Engine" ("." for the same folder).
	Core::String PathBetween(Core::StringView from, Core::StringView to);
}
