// IED's Attach to Process (IED docs/16 "Attaching to a running program"): see attach.cf.

include "bedrock.ch"

namespace Profile
{
	// Listens on the local pipe ied-itp-<pid> for IED's Attach to Process: once, on a thread of its own.
	void ListenForAttach(Core::StringView app);
}
