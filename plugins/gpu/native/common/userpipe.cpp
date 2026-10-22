#include "userpipe.h"

#include <windows.h>
#include <sddl.h>

#include <string>
#include <vector>

namespace fb
{
	// A descriptor whose only access entry is the current user's. LocalFree it.
	static PSECURITY_DESCRIPTOR UserOnly() {
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
		DWORD size = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &size);
		std::vector<BYTE> user(size);
		const BOOL known = size && GetTokenInformation(token, TokenUser, user.data(), size, &size);
		CloseHandle(token);
		wchar_t* sid = nullptr;
		if (!known || !ConvertSidToStringSidW(((TOKEN_USER*)user.data())->User.Sid, &sid)) return nullptr;
		const std::wstring text = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
		LocalFree(sid);
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return nullptr;
		return descriptor;
	}

	void* NewUserPipe(const wchar_t* name, unsigned bufferBytes) {
		PSECURITY_DESCRIPTOR descriptor = UserOnly();
		if (!descriptor) return nullptr;
		SECURITY_ATTRIBUTES security = {sizeof(security), descriptor, FALSE};
		HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, bufferBytes, bufferBytes, 0, &security);
		const DWORD error = GetLastError();
		LocalFree(descriptor);
		SetLastError(error);
		return pipe == INVALID_HANDLE_VALUE ? nullptr : pipe;
	}
}
