// Calls into plugin code (docs/26 sections 2 and 4): server bindings - from another plugin's code, from a UI module
// through plugins.call, from an AI tool - system-message sections a function computes, and the protocol events a
// plugin listens to. Each runs on the plugin's thread; the callers hold the server lock, released while they wait.

include "code.ch"
include "web/rpc.ch"

namespace Ied
{
	// Whether `plugin`'s manifest lists `name` under bindings.consumes.
	bool ConsumesBinding(Plugin* plugin, Core::StringView name);
	// Calls the binding `name` of `provider`'s code with `args` (JSON text; empty: {}) and waits at most
	// IED::kBindingWaitMs or until `stopped()`; a result that is not JSON is an error.
	IED::Result InvokeBinding(Server* server, CodeInstance* provider, Core::StringView name, Core::StringView args, func<bool()> stopped);
	// The text a systemMessageSections entry's `function` gives for `ws`; empty with `problem` when it failed.
	Core::String ComputedSection(Server* server, CodeInstance* instance, Core::StringView function, Workspace* ws, Core::String& problem);
	// On the instance's thread: its handlers of an event's kind, each guarded.
	void DeliverEvent(CodeInstance* instance, Core::StringView topic, Core::StringView kind, Core::StringView data);
	// plugins.call {wsId?, name, args?} -> {result}: a UI module's call of a server binding (docs/10).
	Core::String CallBindingMethod(Server* server, Call& call, const Params& params);

	const int64 kEventsQueued = 1000;   // a plugin's thread falling this far behind drops events
}
