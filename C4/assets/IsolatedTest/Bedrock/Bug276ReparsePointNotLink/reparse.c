#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <string.h>
#include "reparse.h"

enum { kDataBytes = 8, kThirdPartyTag = 0x00000BEE };

int MakeTaggedFile(const char* path)
{
	HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (file == INVALID_HANDLE_VALUE) return (int)GetLastError();
	BYTE buffer[REPARSE_GUID_DATA_BUFFER_HEADER_SIZE + kDataBytes];
	memset(buffer, 0, sizeof(buffer));
	REPARSE_GUID_DATA_BUFFER* reparse = (REPARSE_GUID_DATA_BUFFER*)buffer;
	static const GUID kOwner = { 0x1c4c4c4c, 0x1234, 0x5678, { 1, 2, 3, 4, 5, 6, 7, 8 } };
	reparse->ReparseTag = kThirdPartyTag;
	reparse->ReparseDataLength = kDataBytes;
	reparse->ReparseGuid = kOwner;
	memcpy(reparse->GenericReparseBuffer.DataBuffer, "c4 tests", kDataBytes);
	DWORD returned = 0;
	const BOOL set = DeviceIoControl(file, FSCTL_SET_REPARSE_POINT, buffer, sizeof(buffer), NULL, 0, &returned, NULL);
	const DWORD why = set ? 0 : GetLastError();
	CloseHandle(file);
	return (int)why;
}
