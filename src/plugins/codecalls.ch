// Calls into plugin code (docs/26 sections 2 and 4): server bindings - from another plugin's code, from a UI module
// through plugins.call, from an AI tool - system-message sections a function computes, and the protocol events a
// plugin listens to. Each runs on the plugin's thread; the callers hold the server lock, released while they wait.

include "code.ch"
include "web/rpc.ch"

namespace Ied
{
	// Whether `plugin`'s manifest lists `name` under bindings.consumes.
	bool ConsumesBinding(Plugin* plugin, Core::StringView name);
	// Whether `plugin`'s manifest lists `name` under bindings.provides.
	bool ProvidesBinding(Plugin* plugin, Core::StringView name);
	// Calls the binding `name` of `provider`'s code with `args` (JSON text; empty: {}) and waits at most
	// IED::kBindingWaitMs or until `stopped()`; a result that is not JSON is an error.
	IED::Result InvokeBinding(Server* server, CodeInstance* provider, Core::StringView name, Core::StringView args, func<bool()> stopped);
	// The same, waiting at most `waitMs`.
	IED::Result InvokeBindingWithin(Server* server, CodeInstance* provider, Core::StringView name, Core::StringView args, func<bool()> stopped, int64 waitMs);
	// A workspace server's call of a binding the master answers (master.binding, docs/26 "Servers"), waiting at most
	// `waitMs` for it; the lock is held (released while it waits).
	IED::Result ForwardBinding(Server* server, Core::StringView name, Core::StringView args, int64 waitMs);
	// Whether a plugin loaded for `ws` (null: a global one) says it provides the server binding `name`, wherever its
	// code runs.
	bool ProvidedAnywhere(Server* server, Workspace* ws, Core::StringView name);
	// `name` called where it is provided: by the code loaded for `ws` here, else (a workspace server) the master's.
	IED::Result CallAnywhere(Server* server, Workspace* ws, Core::StringView name, Core::StringView args, func<bool()> stopped, int64 waitMs);

	const int64 kForwardSlackMs = 10000;            // the pipe's wait for a forwarded binding, past the binding's own
	const int64 kOwnBindingForwardMs = 7200000;     // a plugin's call of its own master binding: as long as a build
	// The text a systemMessageSections entry's `function` gives for `ws`; empty with `problem` when it failed.
	Core::String ComputedSection(Server* server, CodeInstance* instance, Core::StringView function, Workspace* ws, Core::String& problem);
	// A plugin's tool call (docs/26 section 8): the `function` of `plugin`'s code loaded for `ws`, or the server binding
	// `entry` names, with `call`'s arguments; waits at most `seconds` or until `stopped()`. With the lock held (released
	// while it waits).
	IED::ToolResult InvokePluginTool(Server* server, Workspace* ws, Plugin* plugin, const Serialization::DomNode& entry, const IED::ToolCall& call, int64 seconds, func<bool()> stopped);
	// The plan of that call for its approval card: the function `entry`'s `plan` names; waits at most IED::kToolPlanMs or
	// until `stopped()`. False with `problem` when it could not be had.
	bool InvokePluginPlan(Server* server, Workspace* ws, Plugin* plugin, const Serialization::DomNode& entry, const IED::ToolCall& call, func<bool()> stopped, IED::ToolPlan& plan, Core::String& problem);
	// On the instance's thread: its handlers of an event's kind, each guarded.
	void DeliverEvent(CodeInstance* instance, Core::StringView topic, Core::StringView kind, Core::StringView data);
	// plugins.call {wsId?, name, args?} -> {result}: a UI module's call of a server binding (docs/10).
	Core::String CallBindingMethod(Server* server, Call& call, const Params& params);

	const int64 kEventsQueued = 1000;   // a plugin's thread falling this far behind drops events
}
