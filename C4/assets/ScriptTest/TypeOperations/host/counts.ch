// What the script's types count as reflection constructs, copies and destroys them in the host's storage.
namespace Counts
{
	int32 g_creates = 0;
	int32 g_deletes = 0;
	int32 g_copies = 0;
}
