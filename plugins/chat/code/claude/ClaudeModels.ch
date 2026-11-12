// The Claude models the CLI offers (docs/04 "The Claude models"; owner, 2026-11-11: "make sure we properly support future
// models automaticly", then "kept up to date with the latest models without needing to do so manually while still having
// access to the old claude models"). The CLI's initialize answer lists them - an alias (opus, sonnet, haiku, fable) or a
// full id each, with the model it resolves to, its name, what it is for and the efforts it takes. IED keeps that list,
// the catalog, as the CLI said it last (the plugin's state, claude-models.json; a workspace server takes it from the
// master with the account's state), and from it:
// - a family's profile (claude-opus: {"model": "opus"}) runs the newest version of the family the catalog has - the
//   alias's model, or a full id newer than it (Fable 5.2 while the fable alias still meant 5.1) - so a model released
//   after this build of IED is used the day the CLI offers it, with nothing to edit;
// - every version the catalog has (a full id: Opus 4.8, Sonnet 5, ...) is a model of its own, named by that id, that a
//   chat can be pinned to, beside ai.models (ReadModels): there while the CLI offers it, never written to the settings;
// - a family the catalog brings that ai.models has no profile of gets one, once (a family taken out stays out).

include "bedrock.ch"
include "api/Plugin.ch"

namespace Ai
{
	class ClaudeCatalogModel
	{
		public Core::String m_Value;          // what --model and set_model take: an alias or a full id
		public Core::String m_Resolved;       // the full id it resolves to
		public Core::String m_Label;          // "Fable 5.2"
		public Core::String m_Description;    // "Most capable for your hardest and longest-running tasks"
		public Core::Array<Core::String> m_Efforts;   // supportedEffortLevels
		public bool m_EffortsSaid;            // the CLI listed them (Haiku 4.5 has none: no effort levels)
		public Core::String m_DefaultEffort;  // the effort the CLI gives it when a request names none (get_settings); empty: none or not known
		public Core::String m_Family;         // "fable" (of the resolved id)
		public Core::Array<int64> m_Version;  // [5, 2]; a date part is not a version
	}

	// The catalog of an initialize answer's `models`, in its order: every model but "default" and the disabled. A model's
	// `defaultEffort` is IED's own field (what WriteClaudeCatalog keeps of ClaudeDefaultEfforts).
	Core::Array<ClaudeCatalogModel*> ReadClaudeCatalog(const Serialization::DomNode& models);
	// The models' default efforts (`efforts`: {value: effort or ""}, from get_settings after set_model of each) given to
	// the catalog; a model it does not name keeps the one `kept` (an earlier catalog) has for the same model.
	void GiveDefaultEfforts(Core::Array<ClaudeCatalogModel*>& catalog, const Serialization::DomNode& efforts, const Core::Array<ClaudeCatalogModel*>& kept);
	// What the default efforts were asked for: the CLI's version and each model with what it resolves to. Asked again
	// when it changes.
	Core::String ClaudeCatalogKey(Core::StringView cliVersion, const Serialization::DomNode& models);
	// The catalog as the CLI writes it (value, resolvedModel, displayName, description, supportedEffortLevels): what
	// ReadClaudeCatalog reads back.
	void WriteClaudeCatalog(IED::JsonWriter& json, const Core::Array<ClaudeCatalogModel*>& catalog);
	// The catalog for the settings and the Model menu (aiusage `claude.models`): each model with its family, whether it is
	// an alias, what an alias's family runs now (ResolveClaudeModel) and whether it is its family's newest.
	void WriteClaudeModels(IED::JsonWriter& json, const Core::Array<ClaudeCatalogModel*>& catalog);

	// The process's catalog: the last the CLI gave (or the master sent). Empty before the CLI was first asked.
	void SetClaudeCatalog(const Core::Array<ClaudeCatalogModel*>& catalog);
	Core::Array<ClaudeCatalogModel*> ClaudeCatalog();

	// What a claude-code profile's `model` runs with this catalog: a full id is itself (a version pinned); an alias, or a
	// family's name, the newest model of its family the catalog has - the alias itself while that is it (the CLI resolves
	// it), else the newer full id. Not in the catalog: as written.
	Core::String ResolveClaudeModel(const Core::Array<ClaudeCatalogModel*>& catalog, Core::StringView model);
	// The profile name of a family: claude-<family>.
	Core::String ClaudeFamilyProfile(Core::StringView family);

	// ai.models (`modelsJson`) kept with the catalog: a family no profile serves and not in `seen` gets claude-<family>
	// (its alias, else its name), named in `added`; what IED's earlier sync wrote that the catalog now gives by itself goes
	// - the CLI's name as a claude-code profile's label, a profile of one version (named by its id, nothing of the
	// owner's in it). False when nothing changed. `seen` gets every family.
	bool TidyClaudeModels(Core::StringView modelsJson, const Core::Array<ClaudeCatalogModel*>& catalog, Core::Array<Core::String>& seen, Core::String& modelsOut, Core::Array<Core::String>& added);
}
