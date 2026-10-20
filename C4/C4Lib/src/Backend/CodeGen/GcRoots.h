#pragma once
#include "Backend/CodeGen/ModuleContext.h"
#include <string>
#include <unordered_set>

namespace Scripting
{
	class ProgramModel;

	/// `__c4_gc_roots` (docs/runtime/01): {address, size} of every global, static member and static local whose
	/// type may hold a managed pointer, ended by {null, 0}. Emitted after every body, so it lists the static
	/// locals those bodies created. In a script, `hostSymbols` names the host's storage, which is not listed, and
	/// there is no `__c4_gc_thread_roots`: the runtime roots each thread's emulated thread_locals.
	void EmitGcRoots(ModuleContext& mctx, const ProgramModel& model, const std::unordered_set<std::string>* hostSymbols = nullptr);
}
