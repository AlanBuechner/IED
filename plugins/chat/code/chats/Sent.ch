// What chat.send brings besides its text (docs/04 "Attachments and @-mentions"): the mentions, checked, and the
// attachments, copied out of their uploads into the chat's media folder, <workspace>/.ied/local/chats/<chatId>/media/.

include "api/Plugin.ch"
include "api/Blobs.ch"
include "Chat.ch"

namespace Ai
{
	const int64 c_MostAttachments = 16;
	const int64 c_AttachmentMaxBytes = 20 * 1024 * 1024;

	// chat.send's `mentions`, [{kind, ref, label}]; throws bad_request for a kind there is not or a mention without the
	// ref its kind needs.
	Core::Array<Mention> SentMentions(const Serialization::DomNode& list);
	// chat.send's `attachments`: [{blobId, name}], each upload written to the chat's media folder, or [{path, name, mime?}],
	// a file that folder already keeps (a queued message's, sent again); throws bad_request past kMostAttachments or
	// kAttachmentMaxBytes or for a path outside the folder, and not_found for an upload or file no longer kept.
	Core::Array<Attachment> KeepAttachments(Chat* chat, const IED::Workspace& ws, const Serialization::DomNode& list);
	// `name` as a media file's name: its last path part, with anything but letters, digits, '.', '-' and '_' made '_'.
	Core::String MediaName(Core::StringView name);
}
