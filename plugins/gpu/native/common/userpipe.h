// userpipe.h - a named pipe instance only the current user may open (a pipe's default descriptor also lets Everyone and
// Anonymous read it), which fails rather than join a pipe another process made under its name first.
#pragma once

namespace fb
{
	// The new overlapped, byte-mode, local-only instance's handle, or null (GetLastError says why).
	void* NewUserPipe(const wchar_t* name, unsigned bufferBytes);
}
