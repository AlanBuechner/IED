#pragma once
#include "Core/Core.h"
#include "Script/HostInterface.h"
#include "Script/ScriptLayer.h"
#include <string>
#include <vector>

namespace Scripting
{
	/// The projects a script's project depends on (docs/runtime/04 section 4.5): those its allow-list names when every
	/// entry names one - of the host, or a layer below it - as a module depends on what its manifest names; none when the
	/// list is empty and `onlyAllowed`. Otherwise - an entry that is a path, or no list a script may go past - the host's
	/// root and every layer below it, which reach every file (ProjectsBelow).
	std::vector<std::string> ScriptDependencies(const HostInterface& host, const std::vector<const ScriptChain*>& layers,
		const std::vector<std::string>& allow, bool onlyAllowed);

	/// The host's root and every layer of `layers`: what a project depends on that may name everything below it.
	std::vector<std::string> ProjectsBelow(const HostInterface& host, const std::vector<const ScriptChain*>& layers);

	/// The interface's and `layers`' units a script compile parses (section 4, step 3), in id order: every file of the
	/// default library; every file of a project the script's project reaches (`depends`, ScriptDependencies'; each
	/// layer's its ScriptLayer::depends); every file adding to a template one of those declares (InterfaceFile::extendedTemplates), and every unit of
	/// a collided symbol (collidedSymbols) one of those declared, each with what its own project reaches. Names a
	/// declaration uses resolve only within its project's reach (docs/language/15 rule 4), so the files left out change
	/// nothing the script's code means.
	std::vector<uint32> ReachedUnits(const HostInterface& host, const std::vector<const ScriptChain*>& layers, const std::vector<std::string>& depends);
} // namespace Scripting
