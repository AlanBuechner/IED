// ProjectManager (docs/03 "Managing projects"): an existing folder made an IED solution or a project of one, and a
// solution's project removed or moved. Each change is made where the solution's projects come from - the project
// system a plugin gives for the solution's build system (its steps change the build system's manifests, then its
// generate writes the IED files again), or IED's own files for a solution written by hand - and the open workspace's
// model is read again at once, so every Solution Explorer shows it without a reload; an open folder made a solution
// becomes one in place. The project.import, project.add, project.remove and project.move methods; members run with the
// server lock held, released while steps run.

include "steps.ch"
include "plugins/host.ch"
include "web/rpc.ch"

namespace Ied
{
	// What a project made of a folder is, as the caller says; an empty field is left to IED or the build system.
	class ProjectOptions
	{
		public Core::String name;
		public Core::String kind;
		public Core::String language;
	}

	class ProjectManager
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// Each throws as its method answers.
		// The folder at the full path `folder` made an IED solution: its project system's generate, else a solution and
		// a project of the folder that IED writes. An open folder workspace there becomes the solution; else it opens.
		public ProjectChange* Import(const FS::Path& folder, const ProjectOptions& options);
		// The folder `folder` of `ws` (workspace-relative, not its root) made a project of its solution; a folder
		// workspace becomes a solution first.
		public ProjectChange* Add(Workspace* ws, Core::StringView folder, const ProjectOptions& options);
		// The project `name` taken out of `ws`'s solution: its folder kept, or moved to the trash with `deleteFiles`.
		public ProjectChange* Remove(Workspace* ws, Core::StringView name, bool deleteFiles);
		// The project `name`'s folder moved to `to` (workspace-relative), the documents open in it following it.
		public ProjectChange* Move(Workspace* ws, Core::StringView name, Core::StringView to);

	private:
		// `operation`'s steps of `system`, planned (with the lock held) for the solution in `folder`.
		private Core::Array<ProjectStep*> Plan(Workspace* ws, const FS::Path& folder, const Serialization::DomNode& system, const char* operation, Core::HashMap<Core::String, Core::String>* values);
		// Runs planned steps with the lock released; throws unavailable with why one failed.
		private void Run(const Core::Array<ProjectStep*>& steps, Core::StringBuilder& log);
		// Writes a file that must not be there yet: through `ws` when it is open (so its watcher knows the write), else
		// straight to disk; `relative` to `folder`.
		private void WriteNew(Workspace* ws, const FS::Path& folder, Core::StringView relative, Core::StringView text);
		// The project's solution file changed by `edit` and written through `ws`.
		private void EditSolution(Workspace* ws, func<bool(Core::StringView, Core::String&, Core::String&)> edit);
		private Core::String ImportMethod(Call& call, const Params& params);
		private Core::String AddMethod(Call& call, const Params& params);
		private Core::String RemoveMethod(Call& call, const Params& params);
		private Core::String MoveMethod(Call& call, const Params& params);

		private Server* server;
	}

	// {workspace, path, project, trashId?, notes, log}
	Core::String ProjectChangeJson(const ProjectChange* change);
}
