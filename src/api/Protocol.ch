// The protocol's own helpers for a plugin's methods (docs/26 section 2b, docs/10): reading a request's params, the
// stable error codes a method answers with, and writing JSON as the server does.

include "web/Params.ch"
include "web/Json.ch"
include "web/Hub.ch"

namespace IED
{
	alias Params = Ied::Params;
	alias RpcError = Ied::RpcError;
	alias ErrorCode = Ied::ErrorCode;
	alias JsonWriter = Ied::JsonWriter;
	// An event whose text field later events of the same kind can append to (a streamed answer's deltas), so a slow
	// client gets them coalesced: {field, text, rest} (docs/10 "Coalescing").
	alias Merge = Ied::Merge;

	// A node's value as JSON text.
	Core::String NodeJson(const Serialization::DomNode& node);
}
