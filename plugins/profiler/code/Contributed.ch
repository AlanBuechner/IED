// Importers other plugins contribute (profilerImporters, docs/16 "Importers"): a command a language plugin runs to turn
// its tool's files (a Go execution trace) into one the profiler reads - Chrome trace JSON, pprof or an .iedtrace.

include "api/Plugin.ch"

namespace Profiler
{
	class ContributedImporter
	{
		public Core::String m_Id;
		public Core::String m_PluginId;
		public Core::String m_Folder;              // the contributing plugin's folder
		public Serialization::DomNode m_Entry;      // as its manifest gives it
	}

	// The importer workspace `wsId`'s plugins contribute for `source`: one naming its extension whose `magic` (when it
	// has one) the file begins with. False when none takes the file.
	bool FindContributedImporter(Core::StringView wsId, Core::StringView source, ContributedImporter& found);

	// What an importer's command is run with.
	class ImporterRun
	{
		public IED::PluginContext* m_Context;
		public IED::Workspace m_Ws;
		public Core::String m_Input;                // the file imported
		public Core::String m_Output;               // the file the command writes
	}

	// Runs the importer's command (`run`, its words expanded: ${input}, ${output}, ${pluginFolder}, ${config:<key>});
	// false with `problem` - the command's own output when it failed.
	bool RunContributedImporter(const ContributedImporter& importer, const ImporterRun& run, Core::String& problem);

	// `word` with the run's variables in it; false with `problem` naming one it does not know.
	bool ExpandImporterWord(Core::StringView word, const ContributedImporter& importer, const ImporterRun& run, Core::String& expanded, Core::String& problem);
}
