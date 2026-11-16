#pragma once
#include "Script/ScriptLayer.h"
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Scripting
{
	/// What a compiled script assumed of its host beyond the interface files it read (docs/runtime/04 section 4.6): the
	/// names it asked the host's table about and the types it asked whether the host registered, each with the answer,
	/// and the host's inline bodies it took, each with its digest (InlineBodyIndex::Digest). A cached script is used only
	/// while every one of them still holds.
	struct HostAssumptions
	{
		std::vector<std::pair<std::string, bool>> symbols;
		std::vector<std::pair<std::string, bool>> types;
		std::vector<std::pair<std::string, std::string>> inlineBodies;
		// The digest of every interface file (InterfaceDigests::Whole) when the compile read them all, as one that meets a
		// collided symbol does; empty when it read what its project reaches, which the cache entry's name covers.
		std::string wholeInterface;
	};

	/// The digests of an interface's files as a script's code depends on them (docs/runtime/04 section 4.6): each file's
	/// text without its blank lines - a cut body leaves only line breaks, so a body edit changes none - and the lines an
	/// `assert_` is on, which an assertion's site records. Made at the first ask, from any thread.
	class InterfaceDigests
	{
	public:
		explicit InterfaceDigests(const HostInterface& host) : m_Host(&host) {}

		/// The digest of the file of unit `unitId` (1-based, in the interface's order).
		const std::string& Of(uint32 unitId);
		/// A digest of every file's.
		const std::string& Whole();

	private:
		void Make();

		const HostInterface* m_Host;
		std::once_flag m_Made;
		std::vector<std::string> m_Files;
		std::string m_Whole;
	};

	/// The digest of `text` as InterfaceDigests makes each file's.
	std::string InterfaceTextDigest(const std::string& text);

	/// Whether every one of `assumptions` holds of the host as `index` (with the layers below the script), its inline
	/// body `chunks` and its files' `digests` describe it.
	bool AssumptionsHold(const HostAssumptions& assumptions, const HostIndex& index, const std::vector<std::string>& chunks, InterfaceDigests& digests);
} // namespace Scripting
